#pragma once

#include <cstdint>

namespace zectrix::sdk {
inline namespace v1 {

// Numeric values are part of the SDK v1 source contract. They are not a
// binary protocol and do not carry ESP-IDF error numbers.
enum class Status : std::uint8_t {
    Ok = 0,
    InvalidArgument = 1,
    InvalidState = 2,
    NotFound = 3,
    NoMemory = 4,
    Busy = 5,
    Conflict = 6,
    IoError = 7,
    Timeout = 8,
    Unsupported = 9,
    InternalError = 10,
};

constexpr bool IsOk(Status status) { return status == Status::Ok; }
const char* StatusName(Status status) noexcept;

}  // namespace v1
}  // namespace zectrix::sdk
