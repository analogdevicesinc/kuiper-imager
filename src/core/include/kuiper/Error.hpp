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

#include <expected>
#include <optional>
#include <string>

namespace kuiper {

enum class ErrorCode {
    NetworkFailure,
    DiskFull,
    PermissionDenied,
    InvalidImage,
    HashMismatch,
    DeviceRemoved,
    DeviceBusy,
    UnsupportedPlatform,
    NotKuiper2,
    NotFound,
    UserCancelled,
    Unknown,
};

struct Error {
    ErrorCode code = ErrorCode::Unknown;
    std::string message;
    std::string details;
    std::optional<std::string> recoverySuggestion;
};

template <typename T>
using Result = std::expected<T, Error>;

inline std::unexpected<Error> Err(
    ErrorCode code, std::string message, std::string details = {},
    std::optional<std::string> hint = std::nullopt) {
    return std::unexpected(
        Error{code, std::move(message), std::move(details), std::move(hint)});
}

const char* toString(ErrorCode code) noexcept;

}  // namespace kuiper
