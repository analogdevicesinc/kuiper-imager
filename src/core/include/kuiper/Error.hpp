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

inline std::unexpected<Error> Err(ErrorCode code, std::string message,
                                  std::string details = {},
                                  std::optional<std::string> hint = std::nullopt) {
    return std::unexpected(
        Error{code, std::move(message), std::move(details), std::move(hint)});
}

const char* toString(ErrorCode code) noexcept;

}  // namespace kuiper
