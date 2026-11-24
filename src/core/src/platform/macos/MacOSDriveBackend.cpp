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

// macOS drive backend — compilable stub (Phase 4). See docs: platform-backends.

#include "kuiper/platform/IDriveBackend.hpp"

namespace kuiper {
namespace {

class MacOSDriveBackend final : public IDriveBackend {
public:
    const char* name() const noexcept override { return "macos"; }

    Result<DriveList> listDrives() override {
        // noop — real enumeration lands with the macOS port (Phase 4).
        return DriveList{};
    }

    Result<MountedPartition> mount(const Partition&) override {
        return std::unexpected(unsupported().error());
    }

    Result<void> unmountAll(const std::string&) override { return unsupported(); }
    Result<void> eject(const std::string&) override { return unsupported(); }

    Result<std::unique_ptr<IRawDevice>> openForWrite(const std::string&) override {
        return std::unexpected(unsupported().error());
    }
    Result<std::unique_ptr<IRawDevice>> openForRead(const std::string&) override {
        return std::unexpected(unsupported().error());
    }

private:
    static Result<void> unsupported() {
        return Err(ErrorCode::UnsupportedPlatform,
                   "Flashing is not yet implemented on macOS");
    }
};

}  // namespace

std::unique_ptr<IDriveBackend> makeDriveBackend() {
    return std::make_unique<MacOSDriveBackend>();
}

}  // namespace kuiper
