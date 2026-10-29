#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace kuiper {

// A query for ImageService::listReleases(). `channel` picks the source; `branch`
// and `limit` apply to the unstable (GitHub Actions) channel only.
struct ReleaseQuery {
    std::string channel = "stable";     // "stable" | "unstable"
    std::optional<std::string> branch;  // filter unstable builds to one branch
    int limit = 10;                     // recent successful runs to scan
};

// One fetchable image (portable: std types only). `id` is the UNIQUE fetch
// handle, scheme-prefixed so fetch routes by provider: "gh:<artifactId>" for
// unstable (the artifact id, since variant+sha isn't unique — one commit yields
// several runs) and "adi:<version>/<variant>/<arch>" for stable. See docs:
// releases-and-sources.
struct Release {
    std::string id;        // UNIQUE fetch handle (gh:… | adi:…)
    std::string channel;   // "stable" | "unstable"
    std::string version;   // "2026.1.1" — release version (stable)
    std::string branch;    // "dev_zip" — head branch of the build (unstable)
    std::string commit;    // "829251c" — short head SHA (unstable)
    std::string variant;   // "full" | "basic" — image build type
    std::string arch;      // "arm32" | "arm64" — target architecture
    std::uint64_t sizeBytes = 0;
    bool available = true;                 // false once the artifact has expired
    std::optional<std::string> expiresAt;  // ISO-8601 (unstable only)
    std::string sourceUri;                 // opaque download URL (the actual bytes)
};

}  // namespace kuiper
