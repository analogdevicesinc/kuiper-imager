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

#include "customize/Planner.hpp"

#include <algorithm>
#include <cstdlib>

namespace kuiper::customize {

namespace {
constexpr std::uint64_t kMiB = 1024ull * 1024ull;
}

Result<std::vector<FeatureDef>> resolveFeatures(
    const std::vector<std::string>& ids) {
    if (ids.empty()) {
        return Err(ErrorCode::NotFound, "No features selected",
                   "Pass one or more --feature <id>, or --all.");
    }
    const auto& catalog = featureCatalog();
    for (const auto& id : ids) {
        const bool known =
            std::any_of(catalog.begin(), catalog.end(),
                        [&](const FeatureDef& f) { return f.id == id; });
        if (!known) {
            std::string valid;
            for (const auto& f : catalog)
                valid += (valid.empty() ? "" : ", ") + f.id;
            return Err(ErrorCode::NotFound, "Unknown feature: " + id,
                       "Valid features: " + valid);
        }
    }
    // Emit in catalog order, de-duplicated — a stable, dependency-friendly order
    // (desktop first, then libraries, then apps).
    std::vector<FeatureDef> out;
    for (const auto& f : catalog)
        if (std::find(ids.begin(), ids.end(), f.id) != ids.end())
            out.push_back(f);
    return out;
}

std::uint64_t autoGrowBytes(std::uint64_t sumSizeMB) {
    return ((sumSizeMB * 13) / 10 + 512) * kMiB;
}

std::uint64_t growBytesFor(const std::vector<FeatureDef>& features,
                           std::optional<std::uint64_t> override_) {
    if (override_) return *override_;
    std::uint64_t sumMB = 0;
    for (const auto& f : features)
        sumMB += static_cast<std::uint64_t>(f.sizeEstimateMB);
    return autoGrowBytes(sumMB);
}

std::optional<std::uint64_t> parseSize(const std::string& text) {
    if (text.empty()) return std::nullopt;
    char* end = nullptr;
    const double val = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || val < 0) return std::nullopt;
    std::string suf(end);
    while (!suf.empty() && (suf.front() == ' ' || suf.front() == '\t'))
        suf.erase(suf.begin());
    std::uint64_t mult = 1;
    if (suf.empty())
        mult = 1;
    else if (suf == "K" || suf == "k" || suf == "KiB")
        mult = kMiB / 1024;
    else if (suf == "M" || suf == "m" || suf == "MiB")
        mult = kMiB;
    else if (suf == "G" || suf == "g" || suf == "GiB")
        mult = 1024ull * kMiB;
    else
        return std::nullopt;
    return static_cast<std::uint64_t>(val * static_cast<double>(mult));
}

std::vector<std::string> aptInstallArgv(const std::string& rootMount,
                                        const FeatureDef& feature) {
    if (feature.packages.empty()) return {};
    const std::string root = rootMount.empty() ? "<rootfs>" : rootMount;
    std::vector<std::string> argv = {"chroot",
                                     root,
                                     "env",
                                     "LC_ALL=C",
                                     "DEBIAN_FRONTEND=noninteractive",
                                     "apt-get",
                                     "install",
                                     "-y"};
    if (!feature.withRecommends) argv.push_back("--no-install-recommends");
    for (const auto& p : feature.packages) argv.push_back(p);
    return argv;
}

std::vector<std::string> chrootShellArgv(const std::string& rootMount,
                                         const std::string& snippet) {
    const std::string root = rootMount.empty() ? "<rootfs>" : rootMount;
    return {"chroot", root, "env", "LC_ALL=C", "/bin/sh", "-c", snippet};
}

std::vector<FeatureDef> filterByArch(const std::vector<FeatureDef>& features,
                                     const std::string& arch) {
    std::vector<FeatureDef> out;
    for (const auto& f : features) {
        if (f.archs.empty() ||
            std::find(f.archs.begin(), f.archs.end(), arch) != f.archs.end())
            out.push_back(f);
    }
    return out;
}

}  // namespace kuiper::customize
