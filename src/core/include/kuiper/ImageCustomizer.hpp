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
#include <vector>

#include "kuiper/Error.hpp"
#include "kuiper/Progress.hpp"
#include "kuiper/customize/CommandRunner.hpp"

namespace kuiper {

// A file a feature installs into the rootfs (apt can't). Content is a Qt resource
// embedded in libkuiper, so it is byte-exact. See docs: customize-pipeline.
struct FeatureFile {
    std::string path;  // absolute path inside the rootfs
    std::string
        resource;  // Qt resource path, e.g. ":/kuiper/desktop/x.service"
    bool executable = false;         // 0755 if true, else 0644
    std::vector<std::string> archs;  // restrict to these arches; empty => all
};

// One installable feature — a desktop, an ADI library, or an ADI application.
// See docs: customize-pipeline (the feature catalog).
struct FeatureDef {
    std::string id;           // stable key, e.g. "iio-oscilloscope"
    std::string label;        // human name
    std::string group;        // "Desktop" | "Libraries" | "Applications"
    std::string description;  // one line

    std::vector<std::string> packages;  // apt-get install args (may be empty)
    bool withRecommends = false;  // install recommends (desktop metapackages)
    std::vector<std::string>
        chrootPost;                  // /bin/sh -c snippets run after install
    int sizeEstimateMB = 0;          // rough, for auto-sizing the grow
    std::vector<std::string> archs;  // apply only to these arches; empty => all
    std::vector<FeatureFile>
        files;  // files written after apt, before chrootPost
};

// The full set of features a user may add, in stable display order. Pure data.
const std::vector<FeatureDef>& featureCatalog();

// What to do to one image.
struct CustomizePlan {
    std::string imagePath;                // the .img to modify in place
    std::vector<std::string> featureIds;  // ids from the catalog
    std::optional<std::uint64_t>
        growBytes;  // nullopt => auto-size; 0 => no grow
    std::optional<std::string>
        archOverride;  // for images lacking /etc/kuiper-release
    // A host script run as root in the chroot after the features, before restamp
    // (copied in, run, removed). A plan needs features, a script, or both.
    std::optional<std::string> scriptPath;
    bool restampVariant = true;  // set KUIPER_VARIANT=custom, refresh pkgs
    bool dryRun = false;         // compute/validate only; touch nothing
};

// What customize() did (or, in dry-run, would do).
struct CustomizeSummary {
    std::vector<std::string> installed;  // feature ids applied, in order
    std::uint64_t finalImageBytes = 0;   // image size after any grow
    std::string detectedArch;            // "armhf" | "arm64"
    std::string ranScript;  // basename of the custom script, if any
};

// Adds ADI full-image features to a basic .img in place. Linux-only and
// privileged. See docs: customize-pipeline.
class ImageCustomizer {
public:
    virtual Result<CustomizeSummary> customize(const CustomizePlan& plan,
                                               ProgressFn onProgress = {},
                                               CancelToken cancel = {}) = 0;
    virtual ~ImageCustomizer() = default;
};

// Factory: the Linux implementation on Linux, a stub returning UnsupportedPlatform
// elsewhere — mirroring makeDriveBackend(). Pass a CommandRunner to inject a fake
// in tests; nullptr uses the production (QProcess) runner.
std::unique_ptr<ImageCustomizer> makeImageCustomizer(
    std::unique_ptr<CommandRunner> runner = nullptr);

namespace customize {

// Shared by all three front-ends (CLI, helper, GUI) so their size grammar and
// auto-grow preview can't drift from the executor.

// Parse a human size ("4G", "512M", "1024K", bare bytes; binary units). Empty or
// malformed => nullopt, which callers treat as "auto".
std::optional<std::uint64_t> parseSize(const std::string& text);

// Auto-grow size in bytes for a summed feature estimate (MB): ~1.3x + 512 MiB.
std::uint64_t autoGrowBytes(std::uint64_t sumSizeMB);

}  // namespace customize

}  // namespace kuiper
