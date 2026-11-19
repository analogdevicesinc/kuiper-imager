#pragma once

#include <string>
#include <vector>

#include "kuiper/Error.hpp"
#include "kuiper/Release.hpp"
#include "kuiper/ReleaseProvider.hpp"
#include "kuiper/http/HttpClient.hpp"

namespace kuiper {

// Serves the stable channel from a hosted JSON manifest, cached on disk with a
// compiled-in seed as the offline fallback. See docs: releases-and-sources.
class StableReleaseProvider : public ReleaseProvider {
public:
    StableReleaseProvider(IHttpClient& http, std::string cacheDir);

    Result<std::vector<Release>> list(const ReleaseQuery& query) override;
    Result<DownloadSource> resolve(const std::string& id) override;

private:
    Result<std::vector<Release>> catalog();

    IHttpClient& http_;
    std::string cacheDir_;
};

}  // namespace kuiper
