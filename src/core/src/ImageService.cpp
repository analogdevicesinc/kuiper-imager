#include "kuiper/ImageService.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

#include "image/GitHubActionsProvider.hpp"
#include "util/Sha256.hpp"

namespace kuiper {

ImageService::ImageService(std::unique_ptr<IHttpClient> http,
                           std::optional<std::string> githubToken)
    : http_(std::move(http)), githubToken_(std::move(githubToken)) {}

Result<std::vector<Release>> ImageService::listReleases(
    const ReleaseQuery& query) {
    if (query.channel == "unstable") {
        GitHubActionsProvider provider(*http_, githubToken_);
        return provider.list(query);
    }
    if (query.channel == "stable") {
        // No official release source is wired up yet (see the plan's Open
        // Questions). Fail cleanly and point at the one channel that works.
        return Err(
            ErrorCode::NotFound, "Stable releases are not yet available",
            "The official Kuiper release source has not been finalised.",
            "Try 'kli list-releases --unstable' to list Kuiper 2.0 CI builds.");
    }
    return Err(ErrorCode::NotFound, "Unknown release channel: " + query.channel,
               "Valid channels are 'stable' and 'unstable'.");
}

Result<FetchSummary> ImageService::fetch(const std::string& id,
                                         const std::string& outputPath, bool force,
                                         ProgressFn onProgress, CancelToken cancel) {
    namespace fs = std::filesystem;

    // Fail fast on local preconditions before touching the network or a token.
    if (!id.starts_with("gh:")) {  // route by identifier scheme (GitHub only yet)
        return Err(ErrorCode::NotFound, "Unknown image identifier: " + id,
                   "Expected 'gh:<id>' from 'kli list-releases --unstable'.");
    }

    // `outputPath` is either a file to write, or a directory to drop the artifact
    // into under its own name. Treat it as a directory if it exists as one, or if
    // it ends with a path separator (an explicit "into this folder" intent).
    const fs::path outArg(outputPath);
    std::error_code ec;
    const bool intoDir =
        fs::is_directory(outArg, ec) ||
        (!outputPath.empty() &&
         (outputPath.back() == '/' ||
          outputPath.back() == static_cast<char>(fs::path::preferred_separator)));

    fs::path outPath = outArg;  // final file path; resolved from the name for dirs
    if (!intoDir) {
        // Explicit file target — refuse an existing file before any network work.
        if (!force && fs::exists(outArg, ec)) {
            return Err(ErrorCode::Unknown,
                       "Output file already exists: " + outputPath,
                       "Choose another path or pass --force to overwrite.");
        }
    } else if (!fs::is_directory(outArg, ec)) {
        return Err(ErrorCode::NotFound,
                   "Output directory does not exist: " + outputPath,
                   "Create it first, or give a full file path.");
    }

    GitHubActionsProvider provider(*http_, githubToken_);
    auto src = provider.resolve(id);
    if (!src) return std::unexpected(src.error());

    if (intoDir) {
        // Save under the artifact's own name, reduced to a bare filename so an
        // unexpected name can never write outside the target directory.
        fs::path name = fs::path(src->filename).filename();
        if (name.empty()) name = "artifact.zip";
        outPath = outArg / name;
        if (!force && fs::exists(outPath, ec)) {
            return Err(ErrorCode::Unknown,
                       "Output file already exists: " + outPath.string(),
                       "Choose another directory or pass --force to overwrite.");
        }
    }

    // Stream to a sibling .part file and rename on success, so a failed or
    // cancelled fetch never leaves a partial file at the destination.
    fs::path partPath = outPath;
    partPath += ".part";
    std::ofstream out(partPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        return Err(ErrorCode::PermissionDenied,
                   "Cannot open output file: " + partPath.string());
    }

    detail::Sha256 hash;
    std::uint64_t written = 0;
    bool writeFailed = false, cancelled = false, diskFull = false, checked = false;

    DownloadSink sink = [&](const char* data, std::size_t n) -> bool {
        if (cancel.isCancelled()) { cancelled = true; return false; }
        out.write(data, static_cast<std::streamsize>(n));
        if (!out) { writeFailed = true; return false; }
        hash.update(reinterpret_cast<const std::byte*>(data), n);
        written += n;
        return true;
    };

    Progress prog;
    prog.phase = Progress::Phase::Downloading;
    DownloadOptions opts;
    opts.onProgress = [&](std::uint64_t now, std::uint64_t total) -> bool {
        if (cancel.isCancelled()) { cancelled = true; return false; }
        if (!checked && total > 0) {  // best-effort space guard once size is known
            checked = true;
            const fs::path dir =
                partPath.parent_path().empty() ? fs::path(".") : partPath.parent_path();
            std::error_code sec;
            const auto space = fs::space(dir, sec);
            if (!sec && space.available < total) { diskFull = true; return false; }
        }
        if (onProgress) {
            prog.bytesDone = now;
            prog.bytesTotal = total;
            prog.fraction =
                total ? static_cast<double>(now) / static_cast<double>(total) : 0.0;
            onProgress(prog);
        }
        return true;
    };

    auto resp = http_->download(src->url, src->headers, sink, opts);
    out.close();
    if (!out) writeFailed = true;  // a failed final flush must not look like success

    // Any non-success path drops the partial file first.
    auto fail = [&](Error e) -> Result<FetchSummary> {
        std::error_code rmec;
        fs::remove(partPath, rmec);
        return std::unexpected(std::move(e));
    };

    if (diskFull) {
        return fail(Error{ErrorCode::DiskFull, "Not enough free space for the image",
                          "The download is larger than the free space at the destination."});
    }
    if (writeFailed) {
        return fail(Error{ErrorCode::Unknown, "Failed to write the output file",
                          partPath.string()});
    }
    if (cancelled) return fail(Error{ErrorCode::UserCancelled, "Fetch cancelled"});
    if (!resp) return fail(resp.error());

    const long s = resp->status;
    if (s == 401 || s == 403) {
        return fail(Error{ErrorCode::PermissionDenied,
                          "The download was rejected (HTTP " + std::to_string(s) + ")",
                          src->url, "The token may lack the 'actions:read' scope."});
    }
    if (s == 404) {
        return fail(Error{ErrorCode::NotFound, "Artifact not found (HTTP 404)",
                          "It may have expired (~90 days) — re-run list-releases."});
    }
    if (s < 200 || s >= 300) {
        return fail(Error{ErrorCode::NetworkFailure,
                          "Download failed (HTTP " + std::to_string(s) + ")", src->url});
    }

    // Success — publish atomically (fall back to copy across filesystems).
    std::error_code renec;
    fs::rename(partPath, outPath, renec);
    if (renec) {
        std::error_code cpec;
        fs::copy_file(partPath, outPath, fs::copy_options::overwrite_existing, cpec);
        std::error_code rmec;
        fs::remove(partPath, rmec);
        if (cpec) {
            return Err(ErrorCode::Unknown,
                       "Could not move the download into place: " + outPath.string());
        }
    }

    if (onProgress) {
        prog.phase = Progress::Phase::Done;
        prog.fraction = 1.0;
        onProgress(prog);
    }
    return FetchSummary{outPath.string(), written, hash.hex()};
}

}  // namespace kuiper
