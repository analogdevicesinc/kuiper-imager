#include "kuiper/Layout.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace kuiper {
namespace {

// Case-insensitive "does `haystack` contain `needle`". Labels vary in case
// across tools/images (BOOT vs boot, rootfs vs ROOTFS), and udev appends a
// suffix on collision (BOOT1), so we match a substring, not equality.
bool containsNoCase(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return false;
    auto lower = [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    };
    auto it = std::search(
        haystack.begin(), haystack.end(), needle.begin(), needle.end(),
        [&](char a, char b) { return lower(a) == lower(b); });
    return it != haystack.end();
}

// First partition of `fsType`, preferring one whose label matches `labelHint`.
// Returns a copy so KuiperLayout never dangles into the Drive.
std::optional<Partition> pickFilesystem(const Drive& drive,
                                        const std::string& fsType,
                                        const std::string& labelHint) {
    const Partition* firstMatch = nullptr;
    for (const Partition& p : drive.partitions) {
        if (p.fsType != fsType) continue;
        if (containsNoCase(p.label, labelHint)) return p;  // best: label + fs
        if (!firstMatch) firstMatch = &p;                  // fallback candidate
    }
    if (firstMatch) return *firstMatch;
    return std::nullopt;
}

// The intel preloader lives on a raw, unformatted, unmounted partition. If a
// card carries several such partitions we take the smallest (the preloader slot
// is tiny; a large unformatted area would be data, not the bootloader).
std::optional<Partition> pickBootloader(const Drive& drive) {
    const Partition* best = nullptr;
    for (const Partition& p : drive.partitions) {
        if (!p.fsType.empty() || !p.mountpoint.empty()) continue;
        if (!best || p.sizeBytes < best->sizeBytes) best = &p;
    }
    if (best) return *best;
    return std::nullopt;
}

}  // namespace

Result<KuiperLayout> identifyLayout(const Drive& drive) {
    if (drive.partitions.empty()) {
        return Err(ErrorCode::NotKuiper2,
                   "No partitions on " + drive.node,
                   "The card looks blank; flash a Kuiper image first.");
    }

    KuiperLayout layout;
    layout.boot = pickFilesystem(drive, "vfat", "BOOT");
    layout.root = pickFilesystem(drive, "ext4", "rootfs");
    layout.bootloader = pickBootloader(drive);
    return layout;
}

}  // namespace kuiper
