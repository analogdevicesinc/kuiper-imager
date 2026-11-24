// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Analog Devices, Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
