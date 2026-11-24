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

#include "kuiper/Error.hpp"

namespace kuiper {

const char* toString(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::NetworkFailure:
            return "NetworkFailure";
        case ErrorCode::DiskFull:
            return "DiskFull";
        case ErrorCode::PermissionDenied:
            return "PermissionDenied";
        case ErrorCode::InvalidImage:
            return "InvalidImage";
        case ErrorCode::HashMismatch:
            return "HashMismatch";
        case ErrorCode::DeviceRemoved:
            return "DeviceRemoved";
        case ErrorCode::DeviceBusy:
            return "DeviceBusy";
        case ErrorCode::UnsupportedPlatform:
            return "UnsupportedPlatform";
        case ErrorCode::NotKuiper2:
            return "NotKuiper2";
        case ErrorCode::NotFound:
            return "NotFound";
        case ErrorCode::UserCancelled:
            return "UserCancelled";
        case ErrorCode::Unknown:
            return "Unknown";
    }
    return "Unknown";
}

}  // namespace kuiper
