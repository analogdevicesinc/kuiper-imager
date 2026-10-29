#pragma once

#include <string>
#include <vector>

#include "kuiper/Error.hpp"
#include "kuiper/Release.hpp"
#include "kuiper/ReleaseProvider.hpp"

namespace kuiper {

// Serves the stable channel from a compiled-in catalog of published Kuiper
// releases: listing and resolution need no network or token, since every
// download URL is derived from the naming convention. See docs:
// releases-and-sources.
class StableReleaseProvider : public ReleaseProvider {
public:
    Result<std::vector<Release>> list(const ReleaseQuery& query) override;
    Result<DownloadSource> resolve(const std::string& id) override;
};

}  // namespace kuiper
