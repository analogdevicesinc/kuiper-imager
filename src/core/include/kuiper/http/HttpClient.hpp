#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "kuiper/Error.hpp"

namespace kuiper {

struct HttpHeader {
    std::string name;
    std::string value;
};

struct HttpResponse {
    long status = 0;   // HTTP status code (e.g. 200, 404); 0 if none received
    std::string body;  // response body (empty for download() — bytes go to the sink)
};

// Receives a chunk of the response body as it arrives. Return false to abort the
// transfer (e.g. a file write failed or the user cancelled).
using DownloadSink = std::function<bool(const char* data, std::size_t n)>;

// Reports transfer progress: bytes so far and the total (0 if the server didn't
// advertise a length). Return false to cancel the transfer.
using DownloadProgressFn =
    std::function<bool(std::uint64_t now, std::uint64_t total)>;

struct DownloadOptions {
    DownloadProgressFn onProgress;  // may be empty (no reporting)
};

// Minimal HTTP seam behind ImageService. `get()` buffers a small body (JSON API
// responses); `download()` streams an arbitrarily large body to a sink without
// buffering (multi-GB images). Injected so callers are unit-testable with a fake.
class IHttpClient {
public:
    virtual Result<HttpResponse> get(const std::string& url,
                                     const std::vector<HttpHeader>& headers = {}) = 0;

    // Streams the response body to `sink` chunk by chunk. Returns the final
    // status (with an empty body). A sink/progress abort surfaces as
    // ErrorCode::UserCancelled; a transport failure as NetworkFailure.
    virtual Result<HttpResponse> download(const std::string& url,
                                          const std::vector<HttpHeader>& headers,
                                          const DownloadSink& sink,
                                          const DownloadOptions& opts = {}) = 0;

    virtual ~IHttpClient() = default;
};

// libcurl-backed client (see src/http/CurlHttpClient.cpp).
std::unique_ptr<IHttpClient> makeHttpClient();

}  // namespace kuiper
