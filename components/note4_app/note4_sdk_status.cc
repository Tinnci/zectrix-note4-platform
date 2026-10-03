#include "note4/sdk/status.h"

namespace note4::sdk {
inline namespace v2 {

const char* StatusName(Status status) noexcept {
    switch (status) {
        case Status::Ok: return "Ok";
        case Status::InvalidArgument: return "InvalidArgument";
        case Status::InvalidState: return "InvalidState";
        case Status::NotFound: return "NotFound";
        case Status::NoMemory: return "NoMemory";
        case Status::Busy: return "Busy";
        case Status::Conflict: return "Conflict";
        case Status::IoError: return "IoError";
        case Status::Timeout: return "Timeout";
        case Status::Unsupported: return "Unsupported";
        case Status::InternalError: return "InternalError";
    }
    return "Unknown";
}

}  // namespace v2
}  // namespace note4::sdk
