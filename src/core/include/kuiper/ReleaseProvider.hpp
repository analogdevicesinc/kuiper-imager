#pragma once

#include <string>
#include <vector>

#include "kuiper/Error.hpp"
#include "kuiper/Release.hpp"
#include "kuiper/http/HttpClient.hpp"

namespace kuiper {

// A ready-to-download location: the URL plus any headers (auth, etc.) the source
// needs, and the artifact's canonical filename so callers can save it under its
// own name. Handed to IHttpClient::download() by ImageService::fetch().
struct DownloadSource {
    std::string url;
    std::vector<HttpHeader> headers;
    std::string filename;  // e.g. "kuiper_full_64_image.zip"
};

// One release source (unstable CI artifacts today, stable Releases later).
// ImageService dispatches to these by channel/identifier scheme. See docs:
// releases-and-sources.
class ReleaseProvider {
public:
    virtual Result<std::vector<Release>> list(const ReleaseQuery& query) = 0;

    // Turn a Release::id (e.g. "gh:<artifactId>") into a downloadable source,
    // including the artifact's canonical filename. May make one lightweight
    // metadata request (e.g. to read the name and check it hasn't expired).
    virtual Result<DownloadSource> resolve(const std::string& id) = 0;

    virtual ~ReleaseProvider() = default;
};

}  // namespace kuiper
