#pragma once

#include <cstdint>

#define NOTE4_SDK_VERSION_MAJOR 2
#define NOTE4_SDK_VERSION_MINOR 0
#define NOTE4_SDK_VERSION_PATCH 0
#define NOTE4_SDK_VERSION_STRING "2.0.0"

namespace note4::sdk {
inline namespace v2 {

struct Version {
    std::uint16_t major;
    std::uint16_t minor;
    std::uint16_t patch;
};

inline constexpr Version kVersion{
    NOTE4_SDK_VERSION_MAJOR,
    NOTE4_SDK_VERSION_MINOR,
    NOTE4_SDK_VERSION_PATCH,
};

}  // namespace v2
}  // namespace note4::sdk
