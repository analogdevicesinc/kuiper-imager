#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "kuiper/Error.hpp"
#include "kuiper/Progress.hpp"
#include "kuiper/Project.hpp"

namespace kuiper {

// Injected raw preloader writer, so ConfigurationService stays portable and never
// touches a drive itself. The CLI wires it to DriveService::writePreloader.
using PreloaderSink =
    std::function<Result<void>(const std::string& preloaderFile, ProgressFn)>;

// Options for configureProject.
struct ConfigureOptions {
    bool dryRun = false;  // compute the copy plan without touching the card
};

// One boot file that configure copies into the boot-partition root.
struct CopyOp {
    std::string source;  // absolute path within the boot partition
    std::string dest;    // absolute path at the boot-partition root
};

// What configureProject did (or, in dry-run, would do).
struct ConfigureSummary {
    std::vector<CopyOp> files;
    bool dryRun = false;
    std::optional<std::string> preloader;  // resolved source, intel only
};

// Reads and applies the ADI boot-file configuration on the mounted BOOT
// partition. Path-based by design (manifests are files in the FAT filesystem), so
// it stays portable and the partition must already be mounted. See docs:
// configure.
class ConfigurationService {
public:
    ConfigurationService() = default;

    // Recursively scan `bootPath` for *.json manifests and collect every project
    // they declare. Malformed/non-manifest JSON is skipped silently; an empty
    // result is not an error, but a missing/non-directory path is.
    Result<ProjectList> listProjects(const std::string& bootPath = "/boot");

    // Find the project matching `name` + `board`; the first match wins. Returns
    // NotFound if none matches.
    Result<Project> findProject(const std::string& bootPath,
                                const std::string& name,
                                const std::string& board);

    // Apply a project's boot files: copy its kernel and files[] into the
    // boot-partition root. Fails fast — a missing source leaves the card
    // untouched. Intel projects also need a raw preloader write: pass
    // `writePreloader`, or without a sink they return the manual procedure.
    Result<ConfigureSummary> configureProject(const std::string& bootPath,
                                              const Project& project,
                                              const ConfigureOptions& opts = {},
                                              ProgressFn onProgress = {},
                                              PreloaderSink writePreloader = {});

    const char* backendName() const noexcept;
};

}  // namespace kuiper
