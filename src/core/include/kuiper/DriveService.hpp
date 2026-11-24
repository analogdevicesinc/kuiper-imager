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

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "kuiper/Drive.hpp"
#include "kuiper/Error.hpp"
#include "kuiper/MountedPartition.hpp"
#include "kuiper/Progress.hpp"
#include "kuiper/platform/IDriveBackend.hpp"

namespace kuiper {

// Wall-clock breakdown of a flash, in seconds — a diagnostic surface for seeing
// where time goes and A/B-ing performance changes. See docs: flash-pipeline.
struct FlashTimings {
    double prepareSec = 0;      // unmount + open + wipe signatures
    double writeSec = 0;        // decompress + device write, serialized
    double decompressSec = 0;   //   of which: archive_read_data
    double deviceWriteSec = 0;  //   of which: dev.write (memcpy + aligned device write)
    double flushSec = 0;        // flushAndSync (fsync)
    double verifySec = 0;       // body read-back + hash compare
    double headSec = 0;         // commit + verify partition table
    double totalSec = 0;        // whole flash() call
};

// Result of a successful flash: bytes written and the SHA-256 of the
// decompressed image (verified against a read-back of the device).
struct FlashSummary {
    std::uint64_t bytesWritten = 0;
    std::string sha256;  // lowercase hex
    // Set by flash(); left empty by writePreloader (which shares this type).
    std::optional<FlashTimings> timings;
};

// Options for a raw write to a block target — shared by flash() and
// writePreloader(). Both fields default to the safe setting. See docs: flash.
struct WriteOptions {
    // Bypass the target-safety guards. For loop devices and power users; the
    // backend still refuses non-block devices. USE WITH CARE.
    bool force = false;

    // Read the target back and compare SHA-256 after writing. On by default.
    bool verify = true;
};

class DriveService {
public:
    DriveService();
    explicit DriveService(std::unique_ptr<IDriveBackend> backend);

    // Enumerate storage drives, each with its partitions filled in.
    Result<DriveList> listDrives();

    // Mount a partition and return an RAII handle to its filesystem path. The
    // handle borrows an existing mount or owns one it creates; either way the
    // mount is valid for the handle's lifetime.
    Result<MountedPartition> mount(const Partition& partition);

    // Unprivileged safe-removal of `driveNode` (unmount + power off). Blocking.
    // See docs: platform-backends (eject).
    Result<void> eject(const std::string& driveNode);

    // Flash a local image file (raw or .zip/.xz/.zst/.gz) onto `driveNode`,
    // then verify by reading it back and comparing SHA-256. Blocking; reports
    // progress via `onProgress` and polls `cancel` between chunks.
    Result<FlashSummary> flash(const std::string& driveNode,
                               const std::string& imagePath,
                               const WriteOptions& options = {},
                               ProgressFn onProgress = {},
                               CancelToken cancel = {});

    // Raw-write an (uncompressed) preloader blob to a partition and verify it by
    // read-back. The caller supplies the partition node from the identified layout
    // (never a composed string). Refuses a mounted/non-removable target unless
    // options.force. See docs: configure (Intel preloader).
    Result<FlashSummary> writePreloader(const std::string& partitionDevice,
                                        const std::string& preloaderFile,
                                        const WriteOptions& options = {},
                                        ProgressFn onProgress = {},
                                        CancelToken cancel = {});

    const char* backendName() const noexcept;

private:
    std::unique_ptr<IDriveBackend> backend_;
};

}  // namespace kuiper
