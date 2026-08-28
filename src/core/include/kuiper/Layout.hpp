#pragma once

#include <optional>

#include "kuiper/Drive.hpp"
#include "kuiper/Error.hpp"

namespace kuiper {

// The three partitions a Kuiper card can carry, each optional. See docs:
// core-library (layout identification).
struct KuiperLayout {
    std::optional<Partition> boot;        // vfat, holds the boot files + manifests
    std::optional<Partition> root;        // ext4 rootfs
    std::optional<Partition> bootloader;  // unformatted partition (intel preloader)
};

// Classify a drive's partitions into Kuiper roles by INSPECTION, never by index —
// the fix for the two-card bug. Pure and platform-free: copies into the optionals,
// holds no pointers into `drive.partitions`. Errors only if `drive` has no
// partitions at all. See docs: core-library (layout identification).
Result<KuiperLayout> identifyLayout(const Drive& drive);

}  // namespace kuiper
