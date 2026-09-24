#pragma once

#include <string>
#include <vector>

#include "kuiper/http/HttpClient.hpp"

namespace kuiper::test {

// Canned IHttpClient: get() returns a fixed JSON body, download() streams fixed
// bytes to the sink. Enough to drive ImageService::fetch end to end without a
// network. Records the last URLs so tests can assert the resolved endpoints.
class FakeHttpClient final : public IHttpClient {
public:
    std::string getBody;
    long getStatus = 200;
    std::string downloadBody;
    long downloadStatus = 200;
    std::string lastGetUrl;
    std::string lastDownloadUrl;

    Result<HttpResponse> get(const std::string& url,
                             const std::vector<HttpHeader>& = {}) override {
        lastGetUrl = url;
        return HttpResponse{getStatus, getBody};
    }

    Result<HttpResponse> download(const std::string& url,
                                  const std::vector<HttpHeader>&,
                                  const DownloadSink& sink,
                                  const DownloadOptions& = {}) override {
        lastDownloadUrl = url;
        if (!downloadBody.empty()) {
            if (!sink(downloadBody.data(), downloadBody.size())) {
                return Err(ErrorCode::UserCancelled, "FakeHttpClient: sink aborted");
            }
        }
        return HttpResponse{downloadStatus, {}};
    }
};

}  // namespace kuiper::test
