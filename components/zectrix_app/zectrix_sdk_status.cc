#include "zectrix/sdk/status.h"

namespace zectrix::sdk {
inline namespace v1 {

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

}  // namespace v1
}  // namespace zectrix::sdk
