#include "kuiper/Error.hpp"

namespace kuiper {

const char* toString(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::NetworkFailure:      return "NetworkFailure";
        case ErrorCode::DiskFull:            return "DiskFull";
        case ErrorCode::PermissionDenied:    return "PermissionDenied";
        case ErrorCode::InvalidImage:        return "InvalidImage";
        case ErrorCode::HashMismatch:        return "HashMismatch";
        case ErrorCode::DeviceRemoved:       return "DeviceRemoved";
        case ErrorCode::DeviceBusy:          return "DeviceBusy";
        case ErrorCode::UnsupportedPlatform: return "UnsupportedPlatform";
        case ErrorCode::NotKuiper2:          return "NotKuiper2";
        case ErrorCode::NotFound:            return "NotFound";
        case ErrorCode::UserCancelled:       return "UserCancelled";
        case ErrorCode::Unknown:             return "Unknown";
    }
    return "Unknown";
}

}  // namespace kuiper
