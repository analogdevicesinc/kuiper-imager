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

#include <memory>
#include <utility>

#include "kuiper/ImageCustomizer.hpp"

namespace kuiper {

#ifdef __linux__

// Defined in LinuxImageCustomizer.cpp (compiled only on Linux).
std::unique_ptr<ImageCustomizer> makeLinuxImageCustomizer(
    std::unique_ptr<CommandRunner> runner);

std::unique_ptr<ImageCustomizer> makeImageCustomizer(
    std::unique_ptr<CommandRunner> runner) {
    return makeLinuxImageCustomizer(std::move(runner));
}

#else

namespace {
class UnsupportedImageCustomizer : public ImageCustomizer {
public:
    Result<CustomizeSummary> customize(const CustomizePlan&, ProgressFn,
                                       CancelToken) override {
        return Err(ErrorCode::UnsupportedPlatform,
                   "Image customization is only supported on Linux",
                   "It relies on loop devices, chroot and binfmt.");
    }
};
}  // namespace

std::unique_ptr<ImageCustomizer> makeImageCustomizer(
    std::unique_ptr<CommandRunner> runner) {
    (void)runner;
    return std::make_unique<UnsupportedImageCustomizer>();
}

#endif

}  // namespace kuiper
