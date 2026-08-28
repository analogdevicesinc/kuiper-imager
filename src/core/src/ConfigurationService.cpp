#include "kuiper/ConfigurationService.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace kuiper {
namespace fs = std::filesystem;

namespace {

std::string toStd(const QString& s) { return s.toStdString(); }

std::string stringField(const QJsonObject& obj, const char* key) {
    const QJsonValue v = obj.value(QLatin1String(key));
    return v.isString() ? toStd(v.toString()) : std::string{};
}

// Build a Project from one `.projects[]` element; entries that aren't objects or
// lack a non-empty name/board are skipped (nullopt).
std::optional<Project> parseProject(const QJsonValue& value,
                                    const std::string& sourceFile) {
    if (!value.isObject()) return std::nullopt;
    const QJsonObject obj = value.toObject();

    Project p;
    p.name = stringField(obj, "name");
    p.board = stringField(obj, "board");
    if (p.name.empty() || p.board.empty()) return std::nullopt;

    p.platform = stringField(obj, "platform");
    p.architecture = stringField(obj, "architecture");
    p.kernel = stringField(obj, "kernel");
    p.preloader = stringField(obj, "preloader");
    p.sourceFile = sourceFile;

    const QJsonValue files = obj.value(QLatin1String("files"));
    if (files.isArray()) {
        for (const QJsonValue& f : files.toArray()) {
            if (!f.isObject()) continue;
            std::string path = stringField(f.toObject(), "path");
            if (!path.empty()) p.files.push_back(std::move(path));
        }
    }
    return p;
}

// Parse one manifest, appending any projects it declares. Unreadable, malformed,
// or non-manifest JSON is tolerated silently.
void collectFromFile(const fs::path& file, ProjectList& out) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return;
    const std::string content((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    if (content.empty()) return;

    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(content));
    if (!doc.isObject()) return;

    const QJsonValue projects = doc.object().value(QLatin1String("projects"));
    if (!projects.isArray()) return;

    const std::string source = file.string();
    for (const QJsonValue& v : projects.toArray()) {
        if (auto p = parseProject(v, source)) out.push_back(std::move(*p));
    }
}

// Resolve a manifest boot-file path to its on-card source. Manifests store paths
// rooted at `/boot`, but the mounted partition may live elsewhere (e.g.
// /run/media/$USER/BOOT).
std::string resolveSource(const std::string& manifestPath,
                          const std::string& bootPath) {
    static const std::string kPrefix = "/boot";
    if (manifestPath.rfind(kPrefix, 0) == 0)
        return bootPath + manifestPath.substr(kPrefix.size());
    return manifestPath;
}

}  // namespace

Result<ProjectList> ConfigurationService::listProjects(const std::string& bootPath) {
    std::error_code ec;
    if (!fs::is_directory(bootPath, ec)) {
        return Err(ErrorCode::NotKuiper2,
                   "Not a Kuiper boot partition: " + bootPath,
                   "Expected the mounted BOOT partition of the card.");
    }

    // Collect *.json paths first, sorted, so output is deterministic regardless
    // of filesystem iteration order.
    std::vector<fs::path> manifests;
    fs::recursive_directory_iterator it(
        bootPath, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        const fs::directory_entry& entry = *it;
        std::error_code fec;
        if (!entry.is_regular_file(fec)) continue;
        if (entry.path().extension() == ".json") manifests.push_back(entry.path());
    }
    std::sort(manifests.begin(), manifests.end());

    ProjectList projects;
    for (const fs::path& m : manifests) collectFromFile(m, projects);

    std::sort(projects.begin(), projects.end(),
              [](const Project& a, const Project& b) {
                  if (a.name != b.name) return a.name < b.name;
                  return a.board < b.board;
              });

    return projects;
}

Result<Project> ConfigurationService::findProject(const std::string& bootPath,
                                                  const std::string& name,
                                                  const std::string& board) {
    auto projects = listProjects(bootPath);
    if (!projects) return std::unexpected(projects.error());

    for (Project& p : *projects) {
        if (p.name == name && p.board == board) return std::move(p);
    }
    return Err(ErrorCode::NotFound,
               "No project '" + name + "' for board '" + board + "'",
               "Run 'kli list-projects --drive <dev>' to see available "
               "projects.");
}

Result<ConfigureSummary> ConfigurationService::configureProject(
    const std::string& bootPath, const Project& project,
    const ConfigureOptions& opts, ProgressFn onProgress,
    PreloaderSink writePreloader) {
    std::error_code ec;
    if (!fs::is_directory(bootPath, ec)) {
        return Err(ErrorCode::NotKuiper2,
                   "Not a Kuiper boot partition: " + bootPath,
                   "Expected the mounted BOOT partition of the card.");
    }

    // A project must name a kernel — the script fails (cp of a "null" path) on a
    // kernel-less manifest, so we reject one upfront before touching the card.
    if (project.kernel.empty()) {
        return Err(ErrorCode::NotFound,
                   "Project '" + project.name + "' declares no kernel",
                   "The manifest entry is missing a 'kernel' path.");
    }

    // Build the copy plan: the kernel plus every files[] entry, each landing in
    // the partition root by basename.
    std::vector<std::string> sources;
    sources.push_back(project.kernel);
    for (const std::string& f : project.files) sources.push_back(f);

    ConfigureSummary summary;
    summary.dryRun = opts.dryRun;
    for (const std::string& s : sources) {
        summary.files.push_back(CopyOp{
            resolveSource(s, bootPath),
            (fs::path(bootPath) / fs::path(s).filename()).string(),
        });
    }

    // Intel additionally needs a raw preloader write to the bootloader
    // partition — a device operation. If a sink is wired in, we perform it;
    // otherwise we refuse upfront (before touching the card) and hand back the
    // exact manual procedure.
    const bool isIntel = project.platform == "intel";
    std::string preloaderSource;
    if (isIntel) {
        preloaderSource = project.preloader.empty()
                              ? std::string{}
                              : resolveSource(project.preloader, bootPath);

        if (!writePreloader) {
            const std::string extlinuxDir =
                (fs::path(bootPath) / "extlinux").string();
            const std::string extlinuxConf =
                (fs::path(bootPath) / "extlinux.conf").string();

            std::string proc = "Intel needs a raw preloader write that isn't "
                               "automated yet. Do it manually:\n";
            for (const CopyOp& op : summary.files)
                proc += "  cp " + op.source + " " + op.dest + "\n";
            // Intel boots via extlinux: the config lives in extlinux/, not root.
            proc += "  mkdir -p " + extlinuxDir + " && mv " + extlinuxConf +
                    " " + extlinuxDir + "/\n";
            proc += "  sudo dd if=" +
                    (preloaderSource.empty() ? "<preloader>" : preloaderSource) +
                    " of=<bootloader-partition, e.g. /dev/mmcblk0p3> bs=1M "
                    "status=progress conv=fsync";
            return Err(ErrorCode::UnsupportedPlatform,
                       "Intel preloader configuration is not yet supported",
                       proc);
        }
        summary.preloader = preloaderSource;
    }

    if (opts.dryRun) return summary;

    // Preflight: every source must exist before we copy anything, so a bad
    // manifest never leaves a half-configured card. For intel, the preloader is
    // part of the all-or-nothing check.
    std::uint64_t totalBytes = 0;
    for (const CopyOp& op : summary.files) {
        std::error_code sec;
        if (!fs::is_regular_file(op.source, sec)) {
            return Err(ErrorCode::NotFound,
                       "Boot file listed in the manifest is missing on the "
                       "card: " + op.source,
                       "The card may be incomplete or from a different image.");
        }
        totalBytes += fs::file_size(op.source, sec);
    }
    if (isIntel) {
        std::error_code sec;
        if (preloaderSource.empty() ||
            !fs::is_regular_file(preloaderSource, sec)) {
            return Err(ErrorCode::NotFound,
                       "Preloader listed in the manifest is missing on the "
                       "card: " +
                           (preloaderSource.empty() ? project.preloader
                                                    : preloaderSource),
                       "The card may be incomplete or from a different image.");
        }
    }

    // Copy each into the boot-partition root, overwriting, with live progress.
    std::uint64_t doneBytes = 0;
    for (const CopyOp& op : summary.files) {
        if (onProgress) {
            Progress p;
            p.phase = Progress::Phase::Configuring;
            p.bytesDone = doneBytes;
            p.bytesTotal = totalBytes;
            p.fraction = totalBytes
                             ? static_cast<double>(doneBytes) / totalBytes
                             : 0.0;
            p.message = fs::path(op.dest).filename().string();
            onProgress(p);
        }

        std::error_code cec;
        fs::copy_file(op.source, op.dest,
                      fs::copy_options::overwrite_existing, cec);
        if (cec) {
            ErrorCode code = ErrorCode::Unknown;
            if (cec.value() == EACCES) code = ErrorCode::PermissionDenied;
            else if (cec.value() == ENOSPC) code = ErrorCode::DiskFull;
            return Err(code, "Failed to copy boot file to " + op.dest,
                       cec.message());
        }

        std::error_code sz;
        doneBytes += fs::file_size(op.source, sz);
    }

    if (isIntel) {
        // Intel boots via extlinux: the config lives in extlinux/, not the root.
        const fs::path extlinuxDir = fs::path(bootPath) / "extlinux";
        const fs::path extlinuxConf = fs::path(bootPath) / "extlinux.conf";
        fs::create_directories(extlinuxDir, ec);
        std::error_code exists_ec;
        if (fs::is_regular_file(extlinuxConf, exists_ec)) {
            std::error_code mv_ec;
            fs::rename(extlinuxConf, extlinuxDir / "extlinux.conf", mv_ec);
            if (mv_ec) {
                return Err(ErrorCode::Unknown,
                           "Failed to move extlinux.conf into extlinux/",
                           mv_ec.message());
            }
        }

        // The raw preloader write, delegated to the injected sink (DriveService).
        if (auto r = writePreloader(preloaderSource, onProgress); !r) {
            return std::unexpected(r.error());
        }
        return summary;
    }

    // Non-intel boards must not carry a stale extlinux/ from a prior intel config.
    // Best-effort — absence is fine.
    fs::remove_all(fs::path(bootPath) / "extlinux", ec);

    if (onProgress) {
        Progress p;
        p.phase = Progress::Phase::Done;
        p.bytesDone = totalBytes;
        p.bytesTotal = totalBytes;
        p.fraction = 1.0;
        onProgress(p);
    }
    return summary;
}

const char* ConfigurationService::backendName() const noexcept { return "portable"; }

}  // namespace kuiper
