#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "kuiper/Error.hpp"
#include "kuiper/Progress.hpp"
#include "kuiper/Release.hpp"
#include "kuiper/http/HttpClient.hpp"

namespace kuiper {

// Result of a successful fetch: where the bytes landed, how many, and their
// SHA-256 (of the file exactly as downloaded).
struct FetchSummary {
    std::string outputPath;
    std::uint64_t bytesWritten = 0;
    std::string sha256;  // lowercase hex
};

// Portable image-source facade — no device, no platform code. Dispatches
// listReleases()/fetch() to a per-channel ReleaseProvider; HTTP is injected so it
// and its providers are unit-testable with a fake. See docs: releases-and-sources.
class ImageService {
public:
    explicit ImageService(std::unique_ptr<IHttpClient> http = makeHttpClient(),
                          std::optional<std::string> githubToken = std::nullopt);

    Result<std::vector<Release>> listReleases(const ReleaseQuery& query = {});

    // Download the artifact named by `id` (e.g. "gh:<artifactId>") to
    // `outputPath`, verbatim (no decompression). Streams to a temporary file and
    // renames on success, so a failed or cancelled fetch never leaves a partial
    // file at outputPath. Refuses to overwrite an existing file unless `force`.
    Result<FetchSummary> fetch(const std::string& id,
                               const std::string& outputPath, bool force = false,
                               ProgressFn onProgress = {}, CancelToken cancel = {});

private:
    std::unique_ptr<IHttpClient> http_;
    std::optional<std::string> githubToken_;
};

}  // namespace kuiper
