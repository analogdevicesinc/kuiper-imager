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

// One fetchable image (portable: std types only). `id` is the UNIQUE fetch handle:
// for the unstable channel it is "gh:<artifactId>", the artifact id (not a
// variant+sha, which isn't unique — one commit yields several runs) with a scheme
// prefix so fetch can route by provider. See docs: releases-and-sources.
struct Release {
    std::string id;        // UNIQUE fetch handle, e.g. "gh:9600752484"
    std::string channel;   // "unstable"
    std::string branch;    // "dev_zip" — head branch of the build (unstable)
    std::string commit;    // "829251c" — short head SHA (unstable)
    std::string variant;   // "full" | "basic" — image build type (unstable)
    std::string arch;      // "arm32" | "arm64" — target architecture (unstable)
    std::uint64_t sizeBytes = 0;
    bool available = true;                 // false once the artifact has expired
    std::optional<std::string> expiresAt;  // ISO-8601 (unstable only)
    std::string sourceUri;                 // opaque download URL (the actual bytes)
};

}  // namespace kuiper
