#include <QCoreApplication>
#include <QProcess>
#include <QString>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "kuiper/ConfigurationService.hpp"
#include "kuiper/DriveService.hpp"
#include "kuiper/ImageService.hpp"
#include "kuiper/Layout.hpp"
#include "kuiper/Progress.hpp"
#include "kuiper/Version.hpp"
#include "kuiper/http/HttpClient.hpp"

namespace {

// Set from the SIGINT handler; polled by the flash CancelToken.
std::atomic<bool> g_cancel{false};
void onSigint(int) { g_cancel.store(true); }

void printUsage() {
    std::puts(
        "kli — Kuiper Imager CLI\n\n"
        "Usage:\n"
        "  kli version\n"
        "  kli list-drives\n"
        "  kli list-releases [--unstable] [--branch <branch>] [--limit <n>]\n"
        "  kli list-projects --drive <dev>\n"
        "  kli fetch --image <id> --output <path> [--force]\n"
        "  kli flash --image <file> --drive <dev> [--yes] [--force]\n"
        "            [--no-verify] [--timings]\n"
        "  kli configure --drive <dev> --project <name> --board <carrier>\n"
        "                [--dry-run] [--force]\n"
        "  kli help\n\n"
        "A <dev> is always a whole drive, e.g. /dev/sda or /dev/mmcblk0.\n"
        "Kuiper Imager finds the BOOT / rootfs / bootloader partitions itself.\n\n"
        "flash options:\n"
        "  -i, --image <file>   Local image: raw .img or .zip/.xz/.zst/.gz/.bz2\n"
        "  -d, --drive <dev>    Target whole drive, e.g. /dev/sdX\n"
        "  -y, --yes            Skip the confirmation prompt\n"
        "      --force          Allow non-removable drives (e.g. /dev/loopN)\n"
        "      --no-verify      Skip the read-back verify (~half the wall time);\n"
        "                       the partition table is still written last & checked\n"
        "      --timings        Print a per-phase wall-clock breakdown\n\n"
        "list-releases options:\n"
        "      --unstable       List Kuiper 2.0 CI builds from GitHub Actions\n"
        "                       (the default 'stable' channel is not wired up yet)\n"
        "  -b, --branch <name>  Filter unstable builds to one branch\n"
        "      --limit <n>      Recent successful runs to scan (default 10)\n"
        "                       Listing needs no token; $GH_TOKEN/$GITHUB_TOKEN or\n"
        "                       'gh auth token' only raises the API rate limit.\n\n"
        "fetch options:\n"
        "  -i, --image <id>     Image identifier from list-releases (e.g. gh:1234)\n"
        "  -o, --output <path>  File to write, or a directory to save the\n"
        "                       artifact into under its own name\n"
        "      --force          Overwrite the output file if it already exists\n"
        "                       Downloading CI artifacts needs a GitHub token with\n"
        "                       'actions:read' ($GH_TOKEN/$GITHUB_TOKEN or gh).\n\n"
        "list-projects options:\n"
        "  -d, --drive <dev>    The card's whole drive; BOOT is mounted for you\n\n"
        "configure options:\n"
        "  -d, --drive <dev>    The card's whole drive; BOOT is mounted for you\n"
        "  -p, --project <name> Eval board / project name (see list-projects)\n"
        "      --board <carrier> Carrier board the project targets\n"
        "      --dry-run        Show the copy plan without touching the card\n"
        "      --force          Allow a non-removable bootloader target (intel)\n\n"
        "Flashing and intel preloader writes require root; run with sudo.\n");
}

void printDrives(const kuiper::DriveList& drives) {
    if (drives.empty()) {
        std::puts("No storage drives found.");
        return;
    }
    std::printf("%-18s %-24s %12s  %-9s %s\n", "DRIVE", "DESCRIPTION", "SIZE(GB)",
                "REMOVABLE", "SYSTEM");
    for (const auto& d : drives) {
        std::printf("%-18s %-24s %12.1f  %-9s %s\n", d.node.c_str(),
                    d.description.c_str(),
                    static_cast<double>(d.sizeBytes) / 1e9,
                    d.isRemovable ? "yes" : "no", d.isSystem ? "yes" : "no");
    }
}

// Stable exit codes so scripts can branch on the failure kind.
int exitCodeFor(kuiper::ErrorCode code) {
    using kuiper::ErrorCode;
    switch (code) {
        case ErrorCode::UserCancelled:    return 3;
        case ErrorCode::PermissionDenied: return 4;
        case ErrorCode::DeviceBusy:       return 5;
        case ErrorCode::DeviceRemoved:    return 6;
        case ErrorCode::InvalidImage:     return 7;
        case ErrorCode::DiskFull:         return 8;
        case ErrorCode::HashMismatch:     return 9;
        case ErrorCode::NotFound:         return 10;
        default:                          return 1;
    }
}

void printError(const kuiper::Error& e) {
    std::fprintf(stderr, "error [%s]: %s\n", kuiper::toString(e.code),
                 e.message.c_str());
    if (!e.details.empty()) {
        std::fprintf(stderr, "  %s\n", e.details.c_str());
    }
    if (e.recoverySuggestion) {
        std::fprintf(stderr, "  hint: %s\n", e.recoverySuggestion->c_str());
    }
}

int cmdListDrives() {
    kuiper::DriveService service;
    const auto result = service.listDrives();
    if (!result) {
        printError(result.error());  // errors → stderr; results → stdout
        return exitCodeFor(result.error().code);
    }
    printDrives(*result);
    return 0;
}

const char* phaseLabel(kuiper::Progress::Phase p) {
    switch (p) {
        case kuiper::Progress::Phase::Preparing:  return "Preparing  ";
        case kuiper::Progress::Phase::Downloading: return "Downloading";
        case kuiper::Progress::Phase::Writing:    return "Writing    ";
        case kuiper::Progress::Phase::Finalizing: return "Finalizing ";
        case kuiper::Progress::Phase::Verifying:  return "Verifying  ";
        case kuiper::Progress::Phase::Configuring: return "Configuring";
        case kuiper::Progress::Phase::Done:       return "Done       ";
    }
    return "";
}

// The --timings phase breakdown (see docs: flash-pipeline).
void printTimings(const kuiper::FlashTimings& t, std::uint64_t bytesWritten) {
    const double mib = static_cast<double>(bytesWritten) / (1024.0 * 1024.0);
    auto rate = [&](double sec) { return sec > 0 ? mib / sec : 0.0; };
    const double residual = t.writeSec - t.decompressSec - t.deviceWriteSec;
    std::fprintf(stderr, "\nTimings (seconds):\n");
    std::fprintf(stderr, "  prepare       %8.1f\n", t.prepareSec);
    std::fprintf(stderr, "  write         %8.1f   (%.0f MiB/s effective)\n",
                 t.writeSec, rate(t.writeSec));
    std::fprintf(stderr, "    decompress  %8.1f\n", t.decompressSec);
    std::fprintf(stderr, "    deviceWrite %8.1f   (%.0f MiB/s)\n",
                 t.deviceWriteSec, rate(t.deviceWriteSec));
    std::fprintf(stderr, "    hash+copy   %8.1f\n", residual);
    std::fprintf(stderr, "  flush         %8.1f\n", t.flushSec);
    std::fprintf(stderr, "  verify        %8.1f   (%.0f MiB/s)\n", t.verifySec,
                 rate(t.verifySec));
    std::fprintf(stderr, "  head          %8.1f\n", t.headSec);
    std::fprintf(stderr, "  total         %8.1f\n", t.totalSec);
}

// Pull "--flag value", "--flag=value", or a short "-f value" out of argv.
bool takeValue(const std::vector<std::string>& args, std::size_t& i,
               const char* lng, const char* shrt, std::string& out) {
    const std::string& a = args[i];
    const std::string eq = std::string(lng) + "=";
    if (a == lng || (shrt && a == shrt)) {
        if (i + 1 >= args.size()) return false;  // missing value
        out = args[++i];
        return true;
    }
    if (a.rfind(eq, 0) == 0) {
        out = a.substr(eq.size());
        return true;
    }
    return false;
}

// A drive's BOOT partition, mounted for the caller's lifetime (borrowed if the
// card was already auto-mounted, otherwise owned and unmounted on destruction).
struct BootContext {
    kuiper::Drive drive;
    kuiper::KuiperLayout layout;
    kuiper::MountedPartition boot;
};

// Resolve --drive, identify its Kuiper partitions, and mount BOOT. Accepts a
// whole-drive node (/dev/sda) or one of its partitions (/dev/sda1) — both resolve
// to the same drive, so a mistyped partition still works instead of erroring.
kuiper::Result<BootContext> openBoot(kuiper::DriveService& svc,
                                     const std::string& driveArg) {
    auto drives = svc.listDrives();
    if (!drives) return std::unexpected(drives.error());

    const kuiper::Drive* found = nullptr;
    for (const auto& d : *drives) {
        if (d.node == driveArg) { found = &d; break; }
        for (const auto& p : d.partitions) {
            if (p.node == driveArg) { found = &d; break; }
        }
        if (found) break;
    }
    if (!found) {
        return kuiper::Err(kuiper::ErrorCode::NotFound,
                           "Drive not found: " + driveArg,
                           "Run 'kli list-drives' to see available drives.");
    }

    auto layout = kuiper::identifyLayout(*found);
    if (!layout) return std::unexpected(layout.error());
    if (!layout->boot) {
        return kuiper::Err(kuiper::ErrorCode::NotKuiper2,
                           "No BOOT partition on " + found->node,
                           "The card doesn't look like a flashed Kuiper card.");
    }

    auto mounted = svc.mount(*layout->boot);
    if (!mounted) return std::unexpected(mounted.error());

    return BootContext{*found, std::move(*layout), std::move(*mounted)};
}

// Discover a GitHub token: env first, then the gh CLI. Lives in the CLI
// (composition root) so the core stays free of environment and gh-CLI assumptions.
std::optional<std::string> discoverGitHubToken() {
    if (const char* t = std::getenv("GH_TOKEN"); t && *t) return std::string(t);
    if (const char* t = std::getenv("GITHUB_TOKEN"); t && *t) {
        return std::string(t);
    }
    QProcess gh;
    gh.start("gh", {"auth", "token"});
    if (gh.waitForStarted(2000) && gh.waitForFinished(4000) &&
        gh.exitStatus() == QProcess::NormalExit && gh.exitCode() == 0) {
        const std::string tok =
            QString::fromUtf8(gh.readAllStandardOutput()).trimmed().toStdString();
        if (!tok.empty()) return tok;
    }
    return std::nullopt;
}

void printReleases(const std::vector<kuiper::Release>& releases) {
    if (releases.empty()) {
        std::puts("No images found.");
        return;
    }
    std::printf("%-16s %-16s %-8s %-7s %-6s %9s  %-22s %s\n", "IDENTIFIER",
                "BRANCH", "COMMIT", "VARIANT", "ARCH", "SIZE(GB)", "EXPIRES",
                "STATUS");
    for (const auto& r : releases) {
        std::printf("%-16s %-16s %-8s %-7s %-6s %9.2f  %-22s %s\n", r.id.c_str(),
                    r.branch.c_str(), r.commit.c_str(), r.variant.c_str(),
                    r.arch.c_str(), static_cast<double>(r.sizeBytes) / 1e9,
                    r.expiresAt ? r.expiresAt->c_str() : "-",
                    r.available ? "available" : "expired");
    }
}

int cmdListReleases(const std::vector<std::string>& args) {
    bool unstable = false;
    std::string branch, limitStr;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--unstable") { unstable = true; continue; }
        if (takeValue(args, i, "--branch", "-b", branch)) continue;
        if (takeValue(args, i, "--limit", nullptr, limitStr)) continue;
        std::fprintf(stderr, "list-releases: unexpected argument '%s'\n",
                     a.c_str());
        return 2;
    }
    if (!branch.empty() && !unstable) {
        std::fprintf(stderr,
                     "list-releases: --branch only applies to --unstable\n");
        return 2;
    }

    kuiper::ReleaseQuery query;
    query.channel = unstable ? "unstable" : "stable";
    if (!branch.empty()) query.branch = branch;
    if (!limitStr.empty()) {
        char* end = nullptr;
        const long n = std::strtol(limitStr.c_str(), &end, 10);
        if (end == limitStr.c_str() || *end != '\0' || n < 1) {
            std::fprintf(stderr,
                         "list-releases: --limit must be a positive number\n");
            return 2;
        }
        query.limit = static_cast<int>(n);
    }

    kuiper::ImageService service(kuiper::makeHttpClient(),
                                 unstable ? discoverGitHubToken() : std::nullopt);
    const auto result = service.listReleases(query);
    if (!result) {
        printError(result.error());
        return exitCodeFor(result.error().code);
    }
    printReleases(*result);
    return 0;
}

void printProjects(const kuiper::ProjectList& projects, const std::string& where) {
    if (projects.empty()) {
        std::printf("No projects found on %s\n", where.c_str());
        return;
    }
    std::printf("%-30s %-26s %-9s %s\n", "EVAL BOARD", "CARRIER", "PLATFORM",
                "ARCH");
    for (const auto& p : projects) {
        std::printf("%-30s %-26s %-9s %s\n", p.name.c_str(), p.board.c_str(),
                    p.platform.c_str(), p.architecture.c_str());
    }
}

int cmdListProjects(const std::vector<std::string>& args) {
    std::string drive;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (takeValue(args, i, "--drive", "-d", drive)) continue;
        std::fprintf(stderr, "list-projects: unexpected argument '%s'\n",
                     args[i].c_str());
        return 2;
    }
    if (drive.empty()) {
        std::fprintf(stderr, "list-projects: --drive is required\n");
        printUsage();
        return 2;
    }

    kuiper::DriveService drives;
    auto boot = openBoot(drives, drive);
    if (!boot) {
        printError(boot.error());
        return exitCodeFor(boot.error().code);
    }

    kuiper::ConfigurationService service;
    const auto result = service.listProjects(boot->boot.path());
    if (!result) {
        printError(result.error());
        return exitCodeFor(result.error().code);
    }
    printProjects(*result, drive);
    return 0;
}

int cmdFetch(const std::vector<std::string>& args) {
    std::string image, output;
    bool force = false;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (takeValue(args, i, "--image", "-i", image)) continue;
        if (takeValue(args, i, "--output", "-o", output)) continue;
        if (a == "--force") { force = true; continue; }
        std::fprintf(stderr, "fetch: unexpected argument '%s'\n", a.c_str());
        return 2;
    }
    if (image.empty() || output.empty()) {
        std::fprintf(stderr, "fetch: --image and --output are required\n");
        printUsage();
        return 2;
    }

    std::signal(SIGINT, onSigint);

    kuiper::ImageService service(kuiper::makeHttpClient(), discoverGitHubToken());

    int lastPercent = -1;
    kuiper::ProgressFn onProgress = [&](const kuiper::Progress& p) {
        const bool done = p.phase == kuiper::Progress::Phase::Done;
        const int pct = done ? 100 : static_cast<int>(p.fraction * 100.0 + 0.5);
        if (pct == lastPercent && !done) return;  // throttle: redraw on % change
        lastPercent = pct;
        std::fprintf(stderr, "\r%s %3d%%", phaseLabel(p.phase), pct);
        std::fflush(stderr);
    };

    kuiper::CancelToken cancel;
    cancel.cancelled = [] { return g_cancel.load(); };

    const auto result =
        service.fetch(image, output, force, std::move(onProgress), cancel);
    std::fprintf(stderr, "\n");  // finish the progress line

    if (!result) {
        printError(result.error());
        return exitCodeFor(result.error().code);
    }

    std::printf("Downloaded %.2f MiB to %s.\n",
                static_cast<double>(result->bytesWritten) / (1024.0 * 1024.0),
                result->outputPath.c_str());
    std::printf("SHA-256: %s\n", result->sha256.c_str());
    std::printf("Next: kli flash --image %s --drive <dev>\n",
                result->outputPath.c_str());
    return 0;
}

int cmdFlash(const std::vector<std::string>& args) {
    std::string image, drive;
    bool assumeYes = false, force = false, showTimings = false, verify = true;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (takeValue(args, i, "--image", "-i", image)) continue;
        if (takeValue(args, i, "--drive", "-d", drive)) continue;
        if (a == "--yes" || a == "-y") { assumeYes = true; continue; }
        if (a == "--force") { force = true; continue; }
        if (a == "--no-verify") { verify = false; continue; }
        if (a == "--timings") { showTimings = true; continue; }
        std::fprintf(stderr, "flash: unexpected argument '%s'\n", a.c_str());
        return 2;
    }
    if (image.empty() || drive.empty()) {
        std::fprintf(stderr, "flash: --image and --drive are required\n");
        printUsage();
        return 2;
    }

    // Loud, unambiguous confirmation — this erases the drive.
    if (!assumeYes) {
        std::fprintf(stderr,
                     "WARNING: this will ERASE ALL DATA on %s\n"
                     "         and write '%s'.\n"
                     "Type 'y' to continue: ",
                     drive.c_str(), image.c_str());
        std::fflush(stderr);
        char line[16] = {0};
        if (!std::fgets(line, sizeof(line), stdin) ||
            (line[0] != 'y' && line[0] != 'Y')) {
            std::fprintf(stderr, "Aborted.\n");
            return 3;
        }
    }

    std::signal(SIGINT, onSigint);

    kuiper::DriveService service;
    kuiper::WriteOptions opts;
    opts.force = force;
    opts.verify = verify;

    int lastPercent = -1;
    kuiper::ProgressFn onProgress = [&](const kuiper::Progress& p) {
        const bool done = p.phase == kuiper::Progress::Phase::Done;
        const int pct =
            done ? 100 : static_cast<int>(p.fraction * 100.0 + 0.5);
        if (pct == lastPercent && !done) {
            return;  // throttle: only redraw on a percent change
        }
        lastPercent = pct;
        std::fprintf(stderr, "\r%s %3d%%", phaseLabel(p.phase), pct);
        std::fflush(stderr);
    };

    kuiper::CancelToken cancel;
    cancel.cancelled = [] { return g_cancel.load(); };

    const auto result =
        service.flash(drive, image, opts, std::move(onProgress), cancel);
    std::fprintf(stderr, "\n");  // finish the progress line

    if (!result) {
        printError(result.error());
        return exitCodeFor(result.error().code);
    }

    if (showTimings && result->timings) {
        printTimings(*result->timings, result->bytesWritten);
    }

    std::printf("Flashed %.2f MiB to %s%s.\n",
                static_cast<double>(result->bytesWritten) / (1024.0 * 1024.0),
                drive.c_str(),
                verify ? " and verified" : " (image not verified)");
    std::printf("SHA-256: %s\n", result->sha256.c_str());
    std::printf("Next: configure the boot files for your hardware project.\n");
    return 0;
}

int cmdConfigure(const std::vector<std::string>& args) {
    std::string drive, project, board;
    bool dryRun = false;
    bool force = false;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (takeValue(args, i, "--drive", "-d", drive)) continue;
        if (takeValue(args, i, "--project", "-p", project)) continue;
        if (takeValue(args, i, "--board", nullptr, board)) continue;
        if (a == "--dry-run") { dryRun = true; continue; }
        if (a == "--force") { force = true; continue; }
        std::fprintf(stderr, "configure: unexpected argument '%s'\n", a.c_str());
        return 2;
    }
    if (drive.empty() || project.empty() || board.empty()) {
        std::fprintf(stderr,
                     "configure: --drive, --project and --board are required\n");
        printUsage();
        return 2;
    }

    kuiper::DriveService drives;
    auto boot = openBoot(drives, drive);
    if (!boot) {
        printError(boot.error());
        return exitCodeFor(boot.error().code);
    }
    const std::string bootPath = boot->boot.path();

    kuiper::ConfigurationService service;
    const auto found = service.findProject(bootPath, project, board);
    if (!found) {
        printError(found.error());
        return exitCodeFor(found.error().code);
    }

    int lastPercent = -1;
    bool progressed = false;
    kuiper::ProgressFn onProgress = [&](const kuiper::Progress& p) {
        const bool done = p.phase == kuiper::Progress::Phase::Done;
        const int pct = done ? 100 : static_cast<int>(p.fraction * 100.0 + 0.5);
        if (pct == lastPercent && !done) return;
        lastPercent = pct;
        progressed = true;
        std::fprintf(stderr, "\r%s %3d%%", phaseLabel(p.phase), pct);
        std::fflush(stderr);
    };

    // Composition root: wire the raw preloader write (intel only) to DriveService,
    // keeping ConfigurationService portable. The bootloader partition comes from
    // the identified layout, never a composed device-node string.
    std::string writtenPart;
    kuiper::PreloaderSink sink =
        [&](const std::string& preloader,
            kuiper::ProgressFn prog) -> kuiper::Result<void> {
        if (!boot->layout.bootloader) {
            return kuiper::Err(
                kuiper::ErrorCode::NotFound,
                "No bootloader partition on " + boot->drive.node,
                "This intel project needs a raw preloader partition, which "
                "this card doesn't have.");
        }
        const std::string part = boot->layout.bootloader->node;
        kuiper::WriteOptions po;
        po.force = force;
        po.verify = true;
        auto r = drives.writePreloader(part, preloader, po, std::move(prog));
        if (!r) return std::unexpected(r.error());
        writtenPart = part;
        return {};
    };

    kuiper::ConfigureOptions opts;
    opts.dryRun = dryRun;
    const auto result = service.configureProject(bootPath, *found, opts,
                                                 std::move(onProgress), sink);
    if (progressed) std::fprintf(stderr, "\n");  // finish the progress line

    if (!result) {
        printError(result.error());
        return exitCodeFor(result.error().code);
    }

    const char* verb = result->dryRun ? "Would copy" : "Copied";
    for (const auto& op : result->files) {
        std::printf("%s %s -> %s\n", verb, op.source.c_str(), op.dest.c_str());
    }
    if (result->preloader) {
        if (result->dryRun) {
            std::printf("Would write preloader %s\n",
                        result->preloader->c_str());
        } else {
            std::printf("Preloader written to %s\n", writtenPart.c_str());
        }
    }
    std::printf("%s %s / %s (%zu files)\n",
                result->dryRun ? "Dry run for" : "Configured",
                project.c_str(), board.c_str(), result->files.size());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    // QCoreApplication: needed for QProcess (lsblk) and Qt event handling.
    QCoreApplication app(argc, argv);

    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "help" || args[0] == "--help") {
        printUsage();
        return args.empty() ? 1 : 0;
    }

    const std::string& cmd = args[0];
    if (cmd == "version" || cmd == "--version") {
        std::printf("kli %s\n", kuiper::version());
        return 0;
    }
    if (cmd == "list-drives") {
        return cmdListDrives();
    }
    if (cmd == "list-releases") {
        return cmdListReleases(args);
    }
    if (cmd == "list-projects") {
        return cmdListProjects(args);
    }
    if (cmd == "fetch") {
        return cmdFetch(args);
    }
    if (cmd == "flash") {
        return cmdFlash(args);
    }
    if (cmd == "configure") {
        return cmdConfigure(args);
    }

    std::fprintf(stderr, "Unknown command: %s\n", cmd.c_str());
    printUsage();
    return 1;
}
