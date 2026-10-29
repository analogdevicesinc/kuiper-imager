#include "StableReleaseProvider.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace kuiper {
namespace {

constexpr const char* kBase = "https://packages.analog.com/public/raw/files";

struct CatalogEntry {
    const char* version;
    const char* variant;  // "full" | "basic"
    const char* arch;     // "arm32" | "arm64"
};

// The published stable releases, newest first. A patch may ship fewer variants
// than its base release (e.g. 2026.1.1 is 64-bit only) — list only what exists.
constexpr CatalogEntry kCatalog[] = {
    {"2026.1.1", "full", "arm64"},
    {"2026.1.1", "basic", "arm64"},
    {"2026.1.0", "full", "arm64"},
    {"2026.1.0", "full", "arm32"},
    {"2026.1.0", "basic", "arm64"},
    {"2026.1.0", "basic", "arm32"},
};

const char* archBits(std::string_view arch) {
    return arch == "arm64" ? "64" : "32";
}

std::string idOf(const CatalogEntry& e) {
    return std::string("adi:") + e.version + "/" + e.variant + "/" + e.arch;
}

std::string imageFilename(const CatalogEntry& e) {
    return std::string("kuiper_") + e.variant + "_" + archBits(e.arch) +
           "_image_" + e.version + ".zip";
}

std::string imageUrl(const CatalogEntry& e) {
    return std::string(kBase) + "/" + imageFilename(e);
}

}  // namespace

Result<std::vector<Release>> StableReleaseProvider::list(const ReleaseQuery&) {
    std::vector<Release> releases;
    for (const auto& e : kCatalog) {
        Release r;
        r.id = idOf(e);
        r.channel = "stable";
        r.version = e.version;
        r.variant = e.variant;
        r.arch = e.arch;
        r.sourceUri = imageUrl(e);
        releases.push_back(std::move(r));
    }
    return releases;
}

Result<DownloadSource> StableReleaseProvider::resolve(const std::string& id) {
    for (const auto& e : kCatalog) {
        if (idOf(e) == id) {
            DownloadSource src;
            src.url = imageUrl(e);
            src.filename = imageFilename(e);
            return src;
        }
    }
    return Err(ErrorCode::NotFound, "Unknown image identifier: " + id,
               "Run 'kli list-releases' for valid stable identifiers.");
}

}  // namespace kuiper
