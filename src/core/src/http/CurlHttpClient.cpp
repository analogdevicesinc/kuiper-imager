#include "kuiper/http/HttpClient.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <curl/curl.h>

namespace kuiper {
namespace {

// libcurl global init is not thread-safe and must run exactly once, before any
// easy handle. A single static instance in makeHttpClient() (C++11 guarantees
// thread-safe once-only init) owns this lifecycle, so individual clients don't.
struct CurlGlobal {
    CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobal() { curl_global_cleanup(); }
    CurlGlobal(const CurlGlobal&) = delete;
    CurlGlobal& operator=(const CurlGlobal&) = delete;
};

// RAII for the two libcurl resources, so every return path frees them.
struct CurlEasyDeleter {
    void operator()(CURL* c) const noexcept { curl_easy_cleanup(c); }
};
using CurlEasy = std::unique_ptr<CURL, CurlEasyDeleter>;

struct CurlSlistDeleter {
    void operator()(curl_slist* s) const noexcept { curl_slist_free_all(s); }
};
using CurlSlist = std::unique_ptr<curl_slist, CurlSlistDeleter>;

// Build a curl_slist from our header pairs (empty list -> null, which is fine).
CurlSlist makeHeaderList(const std::vector<HttpHeader>& headers) {
    curl_slist* raw = nullptr;
    for (const auto& h : headers) {
        raw = curl_slist_append(raw, (h.name + ": " + h.value).c_str());
    }
    return CurlSlist(raw);
}

std::size_t writeCb(char* ptr, std::size_t size, std::size_t nmemb,
                    void* userdata) {
    const std::size_t bytes = size * nmemb;
    static_cast<std::string*>(userdata)->append(ptr, bytes);
    return bytes;
}

// State threaded through the streaming download callbacks. `aborted` is set when
// the sink or progress callback asks to stop, so the caller can tell a deliberate
// abort (CURLE_WRITE_ERROR / CURLE_ABORTED_BY_CALLBACK) from a transport failure.
struct DownloadCtx {
    const DownloadSink* sink = nullptr;
    const DownloadProgressFn* onProgress = nullptr;
    bool aborted = false;
};

std::size_t downloadWriteCb(char* ptr, std::size_t size, std::size_t nmemb,
                            void* userdata) {
    auto* ctx = static_cast<DownloadCtx*>(userdata);
    const std::size_t bytes = size * nmemb;
    if (!(*ctx->sink)(ptr, bytes)) {
        ctx->aborted = true;
        return 0;  // short write -> CURLE_WRITE_ERROR aborts the transfer
    }
    return bytes;
}

int xferInfoCb(void* userdata, curl_off_t dltotal, curl_off_t dlnow,
               curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    auto* ctx = static_cast<DownloadCtx*>(userdata);
    if (!ctx->onProgress || !*ctx->onProgress) return 0;
    // dltotal is the Content-Length once headers arrive, 0 before that (or if the
    // server sends none) — exactly the "0 = unknown" the sink contract expects.
    if (!(*ctx->onProgress)(static_cast<std::uint64_t>(dlnow),
                            static_cast<std::uint64_t>(dltotal))) {
        ctx->aborted = true;
        return 1;  // nonzero -> CURLE_ABORTED_BY_CALLBACK
    }
    return 0;
}

// libcurl-backed HTTP client. Stateless: the global init/cleanup lifecycle lives
// in makeHttpClient() (see CurlGlobal), not here, so this stays thread-safe and
// copy-free.
class CurlHttpClient : public IHttpClient {
public:
    CurlHttpClient() = default;
    CurlHttpClient(const CurlHttpClient&) = delete;
    CurlHttpClient& operator=(const CurlHttpClient&) = delete;

    Result<HttpResponse> get(const std::string& url,
                             const std::vector<HttpHeader>& headers) override {
        CurlEasy curl(curl_easy_init());
        if (!curl) {
            return Err(ErrorCode::NetworkFailure,
                       "Failed to initialise the HTTP client");
        }

        std::string body;
        CurlSlist hdrs = makeHeaderList(headers);

        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, hdrs.get());
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, writeCb);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
        // Never resend Authorization across a cross-host redirect (api.github.com
        // -> blob storage). libcurl's default already restricts it; be explicit.
        curl_easy_setopt(curl.get(), CURLOPT_UNRESTRICTED_AUTH, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 60L);
        curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);  // thread-safe timeouts
        curl_easy_setopt(curl.get(), CURLOPT_ACCEPT_ENCODING, "");  // allow gzip
        // We inspect the status ourselves; don't let curl turn 4xx into an error.
        curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 0L);

        const CURLcode rc = curl_easy_perform(curl.get());
        long status = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);

        if (rc != CURLE_OK) {
            return Err(ErrorCode::NetworkFailure, "HTTP request failed",
                       curl_easy_strerror(rc));
        }
        return HttpResponse{status, std::move(body)};
    }

    Result<HttpResponse> download(const std::string& url,
                                  const std::vector<HttpHeader>& headers,
                                  const DownloadSink& sink,
                                  const DownloadOptions& opts) override {
        CurlEasy curl(curl_easy_init());
        if (!curl) {
            return Err(ErrorCode::NetworkFailure,
                       "Failed to initialise the HTTP client");
        }

        DownloadCtx ctx{&sink, &opts.onProgress, false};
        CurlSlist hdrs = makeHeaderList(headers);

        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, hdrs.get());
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, downloadWriteCb);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &ctx);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, xferInfoCb);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &ctx);
        curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);  // enable xferinfo
        curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_UNRESTRICTED_AUTH, 0L);  // drop auth on redirect
        // No overall CURLOPT_TIMEOUT: a multi-GB image legitimately runs for
        // minutes. Guard against a dead connection instead: abort if throughput
        // stays under 1 KiB/s for 30s.
        curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_LIMIT, 1024L);
        curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_TIME, 30L);
        curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
        // Don't advertise content encodings: we want the bytes on the wire to be
        // the file's bytes (so progress totals and the SHA-256 match the output).
        // Turn a >=400 into an error so the body isn't streamed into the sink.
        curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);

        const CURLcode rc = curl_easy_perform(curl.get());
        long status = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);

        if (ctx.aborted) {
            // Sink or progress callback asked to stop. The caller (which owns the
            // sink) decides whether this was a cancel or a write failure.
            return Err(ErrorCode::UserCancelled, "Download aborted");
        }
        // A >=400 status surfaces as CURLE_HTTP_RETURNED_ERROR; hand the status
        // back so the caller maps it (401 -> PermissionDenied, 404 -> NotFound...).
        if (rc == CURLE_OK || rc == CURLE_HTTP_RETURNED_ERROR) {
            return HttpResponse{status, ""};
        }
        return Err(ErrorCode::NetworkFailure, "Download failed",
                   curl_easy_strerror(rc));
    }
};

}  // namespace

std::unique_ptr<IHttpClient> makeHttpClient() {
    static CurlGlobal global;  // once-only, thread-safe libcurl init; freed at exit
    return std::make_unique<CurlHttpClient>();
}

}  // namespace kuiper
