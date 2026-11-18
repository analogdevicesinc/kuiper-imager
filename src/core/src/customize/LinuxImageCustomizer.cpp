// Linux executor for ImageCustomizer. See docs: customize-pipeline.

#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "customize/Planner.hpp"
#include "kuiper/Drive.hpp"
#include "kuiper/ImageCustomizer.hpp"
#include "kuiper/Layout.hpp"
#include "kuiper/customize/CommandRunner.hpp"

// Register the embedded asset resources. Must be a free function in the GLOBAL
// namespace for the Q_INIT_RESOURCE symbol to resolve (resource in a static lib).
static void kuiperInitAssetResources() { Q_INIT_RESOURCE(kuiper_assets); }

namespace kuiper {
namespace {
namespace fs = std::filesystem;

// RAII LIFO teardown stack (see docs: customize-pipeline).
struct Unwind {
    std::vector<std::function<void()>> actions;
    void add(std::function<void()> f) { actions.push_back(std::move(f)); }
    ~Unwind() {
        for (auto it = actions.rbegin(); it != actions.rend(); ++it)
            if (*it) (*it)();
    }
};

std::string trim(std::string s) {
    const auto ws = " \t\r\n";
    const auto b = s.find_first_not_of(ws);
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

bool hostIsArm() {
    utsname u{};
    if (uname(&u) != 0) return false;
    const std::string m = u.machine;
    return m == "aarch64" || m.rfind("arm", 0) == 0;
}

class LinuxImageCustomizer : public ImageCustomizer {
public:
    explicit LinuxImageCustomizer(std::unique_ptr<CommandRunner> runner)
        : runner_(std::move(runner)) {
        kuiperInitAssetResources();
    }

    Result<CustomizeSummary> customize(const CustomizePlan& plan,
                                       ProgressFn onProgress,
                                       CancelToken cancel) override {
        onProgress_ = std::move(onProgress);

        // A plan needs features, a script, or both; empty on both is a mistake.
        std::vector<FeatureDef> features;
        if (!plan.featureIds.empty()) {
            auto resolved = customize::resolveFeatures(plan.featureIds);
            if (!resolved) return std::unexpected(resolved.error());
            features = std::move(*resolved);
        } else if (!plan.scriptPath) {
            return Err(ErrorCode::NotFound, "Nothing to do",
                       "Select at least one feature or provide a script to run.");
        }
        const std::uint64_t grow = customize::growBytesFor(features, plan.growBytes);

        // Validate the script before any image work (step 2, docs).
        if (plan.scriptPath)
            if (auto v = validateScript(*plan.scriptPath); !v)
                return std::unexpected(v.error());

        if (plan.dryRun) {
            CustomizeSummary s;
            for (const auto& f : features) s.installed.push_back(f.id);
            if (plan.scriptPath)
                s.ranScript = fs::path(*plan.scriptPath).filename().string();
            s.detectedArch = plan.archOverride.value_or("auto");
            std::error_code ec;
            const auto sz = fs::file_size(plan.imagePath, ec);
            s.finalImageBytes = ec ? 0 : sz + grow;
            return s;
        }

        if (auto pc = precheck(plan); !pc) return std::unexpected(pc.error());

        Unwind unwind;
        return runPipeline(plan, features, grow, cancel, unwind);
    }

private:
    std::unique_ptr<CommandRunner> runner_;
    ProgressFn onProgress_;

    void report(Progress::Phase phase, double fraction, std::string msg) {
        if (!onProgress_) return;
        Progress p;
        p.phase = phase;
        p.fraction = fraction;
        p.message = std::move(msg);
        onProgress_(p);
    }

    Result<CommandResult> runChecked(const std::vector<std::string>& argv,
                                     CommandRunner::LineFn onLine = {}) {
        auto r = runner_->run(argv, std::move(onLine));
        if (!r) return std::unexpected(r.error());
        if (r->exitCode != 0)
            return Err(ErrorCode::Unknown,
                       "'" + argv[0] + "' failed (exit " +
                           std::to_string(r->exitCode) + ")",
                       r->output);
        return r;
    }

    void runQuiet(const std::vector<std::string>& argv) {
        (void)runner_->run(argv, {});
    }

    bool haveTool(const std::string& tool) {
        auto r = runner_->run({"sh", "-c", "command -v " + tool}, {});
        return r && r->exitCode == 0;
    }

    Result<void> precheck(const CustomizePlan& plan) {
        if (::geteuid() != 0)
            return Err(ErrorCode::PermissionDenied, "customize must run as root",
                       "Loop-mounting and chroot need root.", "Re-run with sudo.");
        std::error_code ec;
        if (!fs::is_regular_file(plan.imagePath, ec))
            return Err(ErrorCode::NotFound, "Image not found: " + plan.imagePath,
                       "customize modifies a local .img file in place.");
        for (const char* tool : {"losetup", "parted", "sfdisk", "e2fsck",
                                 "resize2fs", "mount", "umount", "chroot",
                                 "blkid", "partprobe"}) {
            if (!haveTool(tool))
                return Err(ErrorCode::UnsupportedPlatform,
                           std::string("Required tool not found: ") + tool,
                           "customize needs util-linux, parted and e2fsprogs.",
                           "Install the missing package and retry.");
        }
        return {};  // qemu posture is validated in planQemu (before any grow).
    }

    Result<std::string> attachLoop(const std::string& image, Unwind& unwind) {
        auto r = runChecked({"losetup", "--show", "-f", "-P", image});
        if (!r) return std::unexpected(r.error());
        const std::string loop = trim(r->output);
        if (loop.empty()) return Err(ErrorCode::Unknown, "losetup returned no device");
        unwind.add([this, loop] { runQuiet({"losetup", "-d", loop}); });
        settlePartitions(loop);
        return loop;
    }

    void settlePartitions(const std::string& loop) {
        runQuiet({"partprobe", loop});
        runQuiet({"partx", "-a", loop});
        runQuiet({"udevadm", "settle", "--timeout=10"});
        for (int i = 0; i < 20; ++i) {
            if (fs::exists(loop + "p1")) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }

    // Enumerate partitions by blkid, classify roles via the shared identifyLayout().
    Result<KuiperLayout> readLayout(const std::string& loop, int& rootNumber,
                                    bool& rootIsLast) {
        Drive drive;
        drive.node = loop;
        for (int n = 1; n <= 16; ++n) {
            const std::string node = loop + "p" + std::to_string(n);
            if (!fs::exists(node)) continue;
            Partition part;
            part.node = node;
            part.number = n;
            if (auto t = runner_->run({"blkid", "-o", "value", "-s", "TYPE", node}, {}))
                part.fsType = trim(t->output);
            if (auto l = runner_->run({"blkid", "-o", "value", "-s", "LABEL", node}, {}))
                part.label = trim(l->output);
            drive.partitions.push_back(std::move(part));
        }
        auto layout = identifyLayout(drive);
        if (!layout) return std::unexpected(layout.error());
        if (!layout->boot || !layout->root)
            return Err(ErrorCode::NotKuiper2,
                       "Image has no recognizable BOOT/rootfs partitions",
                       "This doesn't look like a flashed Kuiper image.");
        rootNumber = layout->root->number;
        // Physically last (greatest end offset), not highest number — see docs.
        rootIsLast = (rootNumber == physicalLastPartition(loop));
        return layout;
    }

    int physicalLastPartition(const std::string& dev) {
        auto r = runner_->run({"sfdisk", "-J", dev}, {});
        if (!r || r->exitCode != 0) return -1;
        const QJsonDocument doc =
            QJsonDocument::fromJson(QByteArray::fromStdString(r->output));
        if (!doc.isObject()) return -1;
        const QJsonArray parts = doc.object()
                                     .value("partitiontable")
                                     .toObject()
                                     .value("partitions")
                                     .toArray();
        long long bestEnd = -1;
        int bestNum = -1;
        for (const QJsonValue& v : parts) {
            const QJsonObject o = v.toObject();
            const std::string node = o.value("node").toString().toStdString();
            int num = 0;
            if (const std::size_t p = node.find_last_not_of("0123456789");
                p != std::string::npos && p + 1 < node.size())
                num = std::stoi(node.substr(p + 1));
            const long long start = static_cast<long long>(o.value("start").toDouble());
            const long long size = static_cast<long long>(o.value("size").toDouble());
            if (start + size > bestEnd) { bestEnd = start + size; bestNum = num; }
        }
        return bestNum;
    }

    // Read the arch from a read-only mount, before the destructive grow (docs).
    Result<std::string> probeArch(const std::string& rootNode) {
        std::error_code ec;
        const std::string dir =
            (fs::temp_directory_path(ec) / "kuiper-arch-probe").string();
        fs::create_directories(dir, ec);
        auto m = runChecked({"mount", "-o", "ro", rootNode, dir});
        if (!m) { fs::remove(dir, ec); return std::unexpected(m.error()); }
        std::string arch;
        {
            std::ifstream rel(dir + "/etc/kuiper-release");
            std::string line;
            while (rel && std::getline(rel, line)) {
                const std::string key = "KUIPER_ARCH=";
                if (line.rfind(key, 0) == 0) {
                    std::string v = trim(line.substr(key.size()));
                    if (!v.empty() && v.front() == '"') v = v.substr(1);
                    if (!v.empty() && v.back() == '"') v.pop_back();
                    arch = v;
                    break;
                }
            }
        }
        runQuiet({"umount", dir});
        fs::remove(dir, ec);
        if (arch.empty())
            return Err(ErrorCode::NotKuiper2,
                       "Could not determine the image architecture",
                       "No KUIPER_ARCH in /etc/kuiper-release.",
                       "Pass --arch armhf or --arch arm64.");
        return arch;
    }

    struct QemuStage {
        bool copyInterpreter = false;  // false => kernel holds it (binfmt F flag)
        std::string interpreter;
    };

    // Validate the qemu/binfmt posture for the arch without touching the image.
    // See docs: customize-pipeline (Architecture detection and the chroot).
    Result<QemuStage> planQemu(const std::string& arch) {
        if (hostIsArm()) return QemuStage{};
        std::string node;
        if (arch == "arm64") node = "/proc/sys/fs/binfmt_misc/qemu-aarch64";
        else if (arch == "armhf") node = "/proc/sys/fs/binfmt_misc/qemu-arm";
        else return Err(ErrorCode::UnsupportedPlatform,
                        "Unsupported image architecture: " + arch);

        std::ifstream in(node);
        if (!in)
            return Err(ErrorCode::UnsupportedPlatform,
                       "No binfmt_misc entry for " + arch + " on this host",
                       "The kernel can't run " + arch + " binaries without qemu "
                       "user-mode emulation.",
                       "sudo apt install qemu-user-binfmt binfmt-support");
        bool enabled = false, hasFix = false;
        std::string interpreter, line;
        while (std::getline(in, line)) {
            if (line == "enabled") enabled = true;
            else if (line.rfind("interpreter ", 0) == 0)
                interpreter = trim(line.substr(std::string("interpreter ").size()));
            else if (line.rfind("flags:", 0) == 0)
                hasFix = line.find('F') != std::string::npos;
        }
        if (!enabled)
            return Err(ErrorCode::UnsupportedPlatform,
                       "binfmt for " + arch + " is registered but disabled", "",
                       "sudo update-binfmts --enable qemu-" +
                           std::string(arch == "arm64" ? "aarch64" : "arm"));
        if (hasFix) return QemuStage{false, interpreter};
        if (interpreter.empty() || !fs::exists(interpreter))
            return Err(ErrorCode::UnsupportedPlatform,
                       "qemu interpreter for " + arch + " is unavailable",
                       "binfmt lacks the 'F' (fix_binary) flag, so the interpreter "
                       "must be copyable into the chroot.",
                       "Install qemu-user-static, or re-register binfmt with "
                       "--fix-binary yes.");
        return QemuStage{true, interpreter};
    }

    Result<void> applyQemu(const QemuStage& stage, const std::string& mountDir,
                           Unwind& unwind) {
        if (!stage.copyInterpreter) return {};
        const std::string dst = mountDir + stage.interpreter;
        std::error_code ec;
        fs::create_directories(fs::path(dst).parent_path(), ec);
        fs::copy_file(stage.interpreter, dst, fs::copy_options::overwrite_existing, ec);
        if (ec)
            return Err(ErrorCode::Unknown,
                       "Could not stage the qemu interpreter: " + ec.message());
        unwind.add([dst] { std::error_code e; fs::remove(dst, e); });
        return {};
    }

    void mountVirtualFs(const std::string& mountDir) {
        runQuiet({"mount", "--bind", "/dev", mountDir + "/dev"});
        runQuiet({"mount", "-t", "proc", "proc", mountDir + "/proc"});
        runQuiet({"mount", "-t", "sysfs", "sys", mountDir + "/sys"});
        runQuiet({"mount", "-t", "devpts", "devpts", mountDir + "/dev/pts"});
    }

    void setupResolv(const std::string& mountDir, Unwind& unwind) {
        const std::string resolv = mountDir + "/etc/resolv.conf";
        const std::string orig = resolv + ".kuiper-orig";
        std::error_code ec;
        bool saved = false;
        if (fs::exists(resolv, ec) || fs::is_symlink(resolv, ec)) {
            fs::rename(resolv, orig, ec);
            saved = !ec;
        }
        {
            std::ifstream host("/etc/resolv.conf");
            std::ofstream out(resolv, std::ios::trunc);
            if (host && out) out << host.rdbuf();
            else if (out) out << "nameserver 8.8.8.8\n";
        }
        unwind.add([resolv, orig, saved] {
            std::error_code e;
            fs::remove(resolv, e);
            if (saved) fs::rename(orig, resolv, e);
        });
    }

    // Write a feature's embedded asset files into the rootfs, honoring each arch.
    Result<void> writeFeatureFiles(const FeatureDef& f, const std::string& mountDir,
                                   const std::string& arch) {
        for (const auto& file : f.files) {
            if (!file.archs.empty() &&
                std::find(file.archs.begin(), file.archs.end(), arch) ==
                    file.archs.end())
                continue;
            QFile res(QString::fromStdString(file.resource));
            if (!res.open(QIODevice::ReadOnly))
                return Err(ErrorCode::Unknown,
                           "Missing embedded asset " + file.resource);
            const QByteArray bytes = res.readAll();
            const std::string dst = mountDir + file.path;
            std::error_code ec;
            fs::create_directories(fs::path(dst).parent_path(), ec);
            std::ofstream out(dst, std::ios::binary | std::ios::trunc);
            if (!out)
                return Err(ErrorCode::Unknown,
                           "Could not write " + file.path + " into the image");
            out.write(bytes.constData(), bytes.size());
            out.close();
            fs::permissions(dst,
                            file.executable
                                ? fs::perms::owner_all | fs::perms::group_read |
                                      fs::perms::group_exec | fs::perms::others_read |
                                      fs::perms::others_exec
                                : fs::perms::owner_read | fs::perms::owner_write |
                                      fs::perms::group_read | fs::perms::others_read,
                            fs::perm_options::replace, ec);
        }
        return {};
    }

    Result<void> installFeatures(const std::vector<FeatureDef>& features,
                                 const std::string& mountDir,
                                 const std::string& arch,
                                 const CancelToken& cancel,
                                 std::vector<std::string>& installed) {
        // apt lines carry the current feature's base fraction (not 0), so the GUI
        // bar doesn't thrash; `base` advances per feature below.
        double base = 0.0;
        auto stream = [this, &base](const std::string& line) {
            report(Progress::Phase::Configuring, base, line);
        };

        report(Progress::Phase::Configuring, 0.0, "apt-get update");
        if (auto r = runChecked(customize::chrootShellArgv(mountDir, "apt-get update"),
                                stream);
            !r)
            return std::unexpected(toAptError(r.error()));

        const double total = static_cast<double>(features.size());
        for (std::size_t i = 0; i < features.size(); ++i) {
            if (cancel.isCancelled())
                return Err(ErrorCode::UserCancelled, "Cancelled",
                           "Stopped at a feature boundary; the image may have a "
                           "partial feature set.");
            const FeatureDef& f = features[i];
            base = static_cast<double>(i) / total;
            report(Progress::Phase::Configuring, base, "Installing " + f.label);

            if (auto argv = customize::aptInstallArgv(mountDir, f); !argv.empty())
                if (auto r = runChecked(argv, stream); !r)
                    return std::unexpected(toAptError(r.error()));
            if (auto r = writeFeatureFiles(f, mountDir, arch); !r)
                return std::unexpected(r.error());
            for (const auto& snippet : f.chrootPost)
                if (auto r = runChecked(customize::chrootShellArgv(mountDir, snippet),
                                        stream);
                    !r)
                    return std::unexpected(r.error());
            installed.push_back(f.id);
        }
        return {};
    }

    // Validate the script on the host (exists + parses under `bash -n`) before any
    // image work, so a typo fails here, not mid-chroot. See docs (step 2).
    Result<void> validateScript(const std::string& hostPath) {
        std::error_code ec;
        if (!fs::is_regular_file(hostPath, ec))
            return Err(ErrorCode::NotFound, "Script not found: " + hostPath,
                       "The custom script must be a readable file on the host.");
        auto r = runner_->run({"bash", "-n", hostPath}, {});
        if (!r) return std::unexpected(r.error());  // couldn't even launch bash
        if (r->exitCode != 0)
            return Err(ErrorCode::Unknown, "The custom script has a syntax error",
                       r->output,
                       "Fix the script (it must parse under 'bash -n') and retry.");
        return {};
    }

    // Run a host script as root in the prepared chroot: copy into /tmp, run,
    // remove. See docs (step 10).
    Result<void> runScript(const std::string& mountDir, const std::string& hostPath,
                           Unwind& unwind) {
        const std::string base = fs::path(hostPath).filename().string();
        if (base.empty())
            return Err(ErrorCode::NotFound, "Invalid script path: " + hostPath);
        const std::string rel = "/tmp/" + base;
        const std::string dst = mountDir + rel;
        report(Progress::Phase::Configuring, 0.85, "Running custom script " + base);

        std::error_code ec;
        fs::copy_file(hostPath, dst, fs::copy_options::overwrite_existing, ec);
        if (ec)
            return Err(ErrorCode::Unknown,
                       "Could not copy the script into the image: " + ec.message());
        fs::permissions(dst,
                        fs::perms::owner_all | fs::perms::group_read |
                            fs::perms::group_exec | fs::perms::others_read |
                            fs::perms::others_exec,
                        ec);
        unwind.add([dst] { std::error_code e; fs::remove(dst, e); });

        auto stream = [this](const std::string& line) {
            report(Progress::Phase::Configuring, 0.85, line);
        };
        // `bash <path>` so a script without a shebang or exec bit still runs.
        if (auto r = runChecked(customize::chrootShellArgv(mountDir, "bash " + rel),
                                stream);
            !r)
            return std::unexpected(toAptError(r.error()));
        return {};
    }

    Error toAptError(Error e) {
        const std::string& o = e.details;
        if (o.find("Could not resolve") != std::string::npos ||
            o.find("Temporary failure resolving") != std::string::npos ||
            o.find("Failed to fetch") != std::string::npos ||
            o.find("Unable to connect") != std::string::npos) {
            e.code = ErrorCode::NetworkFailure;
            e.recoverySuggestion =
                "The chroot needs network access to reach the apt repos.";
        }
        return e;
    }

    // Restamp the image identity so readRelease() reports it honestly (docs).
    void restamp(const std::string& mountDir) {
        rewriteVariantShell(mountDir + "/etc/kuiper-release");
        rewriteVariantJson(mountDir + "/boot/kuiper-release.json");
        if (auto r = runner_->run(
                customize::chrootShellArgv(
                    mountDir,
                    "dpkg-query -W -f '${Package} ${Version}\\n' | sort"),
                {});
            r && r->exitCode == 0) {
            std::ofstream out(mountDir + "/etc/kuiper-packages", std::ios::trunc);
            if (out) out << r->output;
        }
    }

    void rewriteVariantShell(const std::string& path) {
        std::ifstream in(path);
        if (!in) return;
        std::string content, line;
        bool changed = false;
        while (std::getline(in, line)) {
            if (line.rfind("KUIPER_VARIANT=", 0) == 0) {
                content += "KUIPER_VARIANT=\"custom\"\n";
                changed = true;
            } else {
                content += line + "\n";
            }
        }
        in.close();
        if (!changed) return;
        std::ofstream out(path, std::ios::trunc);
        if (out) out << content;
    }

    void rewriteVariantJson(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return;
        const std::string content((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
        in.close();
        QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(content));
        if (!doc.isObject()) return;
        QJsonObject obj = doc.object();
        obj["variant"] = "custom";
        doc.setObject(obj);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (out) {
            const QByteArray bytes = doc.toJson(QJsonDocument::Indented);
            out.write(bytes.constData(), bytes.size());
        }
    }

    Result<CustomizeSummary> runPipeline(const CustomizePlan& plan,
                                         const std::vector<FeatureDef>& features,
                                         std::uint64_t grow,
                                         const CancelToken& cancel, Unwind& unwind) {
        report(Progress::Phase::Preparing, 0.0, "Attaching image");
        auto loop = attachLoop(plan.imagePath, unwind);
        if (!loop) return std::unexpected(loop.error());

        int rootNumber = 0;
        bool rootIsLast = false;
        auto layout = readLayout(*loop, rootNumber, rootIsLast);
        if (!layout) return std::unexpected(layout.error());
        const std::string rootNode = layout->root->node;
        const std::string bootNode = layout->boot->node;

        // Arch + qemu validation BEFORE the destructive grow.
        std::string arch;
        if (plan.archOverride) arch = *plan.archOverride;
        else {
            auto probed = probeArch(rootNode);
            if (!probed) return std::unexpected(probed.error());
            arch = *probed;
        }
        auto qemu = planQemu(arch);
        if (!qemu) return std::unexpected(qemu.error());

        if (grow > 0) {
            if (!rootIsLast)
                return Err(ErrorCode::InvalidImage,
                           "Cannot grow: the rootfs is not the last partition",
                           "Re-run with --no-grow, or pre-enlarge the image.");
            report(Progress::Phase::Preparing, 0.1, "Growing rootfs");
            if (auto r = growRootfs(plan.imagePath, *loop, rootNumber, rootNode, grow);
                !r)
                return std::unexpected(r.error());
        }

        std::error_code ec;
        const std::string mountDir =
            (fs::temp_directory_path(ec) / "kuiper-customize").string();
        fs::create_directories(mountDir, ec);
        if (ec)
            return Err(ErrorCode::Unknown,
                       "Could not create mount point: " + ec.message());
        report(Progress::Phase::Preparing, 0.2, "Mounting rootfs");
        if (auto r = runChecked({"mount", rootNode, mountDir}); !r)
            return std::unexpected(r.error());
        unwind.add([this, mountDir] {
            runQuiet({"sync"});
            runQuiet({"umount", "-R", mountDir});
            runQuiet({"umount", "-R", "-l", mountDir});
            std::error_code e;
            fs::remove(mountDir, e);
        });
        if (auto r = runChecked({"mount", bootNode, mountDir + "/boot"}); !r)
            return std::unexpected(r.error());

        report(Progress::Phase::Preparing, 0.3, "Preparing chroot");
        if (auto r = applyQemu(*qemu, mountDir, unwind); !r)
            return std::unexpected(r.error());
        mountVirtualFs(mountDir);
        setupResolv(mountDir, unwind);

        auto archFeatures = customize::filterByArch(features, arch);
        CustomizeSummary summary;
        summary.detectedArch = arch;
        if (auto r = installFeatures(archFeatures, mountDir, arch, cancel,
                                     summary.installed);
            !r)
            return std::unexpected(r.error());

        // Script runs last, before the restamp, so its packages reach the manifest.
        if (plan.scriptPath && !cancel.isCancelled()) {
            if (auto r = runScript(mountDir, *plan.scriptPath, unwind); !r)
                return std::unexpected(r.error());
            summary.ranScript = fs::path(*plan.scriptPath).filename().string();
        }

        if (plan.restampVariant) {
            report(Progress::Phase::Finalizing, 0.9, "Restamping image identity");
            restamp(mountDir);
        }

        report(Progress::Phase::Finalizing, 0.95, "Flushing and unmounting");
        runQuiet({"sync"});
        // unwind runs here: restore resolv -> remove qemu -> umount -R -> detach.

        const auto sz = fs::file_size(plan.imagePath, ec);
        summary.finalImageBytes = ec ? 0 : sz;
        report(Progress::Phase::Done, 1.0, "Done");
        return summary;
    }

    Result<void> growRootfs(const std::string& image, const std::string& loop,
                            int rootNumber, const std::string& rootNode,
                            std::uint64_t grow) {
        std::error_code ec;
        const auto cur = fs::file_size(image, ec);
        if (ec) return Err(ErrorCode::Unknown, "Cannot size image: " + ec.message());
        fs::resize_file(image, cur + grow, ec);
        if (ec)
            return Err(ErrorCode::DiskFull,
                       "Could not enlarge the image file: " + ec.message(),
                       "Not enough free space on the host for the grow.");
        if (auto r = runChecked({"losetup", "-c", loop}); !r)
            return std::unexpected(r.error());
        if (auto r = runChecked({"parted", "-s", loop, "resizepart",
                                 std::to_string(rootNumber), "100%"});
            !r)
            return std::unexpected(r.error());
        runQuiet({"partprobe", loop});
        settlePartitions(loop);
        if (auto r = runChecked({"e2fsck", "-f", "-y", rootNode}); !r)
            return std::unexpected(r.error());
        if (auto r = runChecked({"resize2fs", rootNode}); !r)
            return std::unexpected(r.error());
        return {};
    }
};

}  // namespace

std::unique_ptr<ImageCustomizer> makeLinuxImageCustomizer(
    std::unique_ptr<CommandRunner> runner) {
    if (!runner) runner = makeCommandRunner();
    return std::make_unique<LinuxImageCustomizer>(std::move(runner));
}

}  // namespace kuiper
