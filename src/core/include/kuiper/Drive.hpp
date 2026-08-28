#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kuiper {

// Pure enumeration facts about one partition. Roles are derived by
// identifyLayout(), not stored here. The `node` is taken verbatim from the
// platform enumeration, never composed by string surgery — composing is what made
// the old guess collide across two identical cards.
struct Partition {
    std::string node;             // "/dev/mmcblk0p1" — verbatim from the backend
    int number = 0;               // trailing integer of the node name
    std::uint64_t sizeBytes = 0;
    std::string fsType;           // "vfat" | "ext4" | "" (unformatted = preloader)
    std::string label;            // hint only — NOT unique across cards
    std::string mountpoint;       // "" if not mounted
};

// A whole removable medium (SD card / USB flash drive). This is the one
// identifier the user supplies (e.g. /dev/sda); Kuiper Imager derives the
// partitions itself.
struct Drive {
    std::string node;             // "/dev/sda" — the ONE id the user supplies
    std::string description;      // model
    std::uint64_t sizeBytes = 0;
    bool isRemovable = false;
    bool isSystem = false;
    std::vector<Partition> partitions;  // empty for a blank / unpartitioned card
};

using DriveList = std::vector<Drive>;

}  // namespace kuiper
