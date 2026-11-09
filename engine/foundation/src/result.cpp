#include "aether/foundation/result.hpp"

namespace aether {

std::string_view to_string(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::Ok: return "ok";
        case ErrorCode::Unknown: return "unknown";
        case ErrorCode::InvalidArgument: return "invalid_argument";
        case ErrorCode::InvalidState: return "invalid_state";
        case ErrorCode::NotFound: return "not_found";
        case ErrorCode::AlreadyExists: return "already_exists";
        case ErrorCode::OutOfMemory: return "out_of_memory";
        case ErrorCode::Unsupported: return "unsupported";
        case ErrorCode::Timeout: return "timeout";
        case ErrorCode::Cancelled: return "cancelled";
        case ErrorCode::PermissionDenied: return "permission_denied";
        case ErrorCode::Conflict: return "conflict";
        case ErrorCode::IoError: return "io_error";
        case ErrorCode::DeviceLost: return "device_lost";
    }
    return "unknown";
}

}  // namespace aether
