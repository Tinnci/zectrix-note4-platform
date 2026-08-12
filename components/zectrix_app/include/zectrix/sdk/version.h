#pragma once

#include <cstdint>

#define ZECTRIX_SDK_VERSION_MAJOR 1
#define ZECTRIX_SDK_VERSION_MINOR 0
#define ZECTRIX_SDK_VERSION_PATCH 0
#define ZECTRIX_SDK_VERSION_STRING "1.0.0"

namespace zectrix::sdk {
inline namespace v1 {

struct Version {
    std::uint16_t major;
    std::uint16_t minor;
    std::uint16_t patch;
};

inline constexpr Version kVersion{
    ZECTRIX_SDK_VERSION_MAJOR,
    ZECTRIX_SDK_VERSION_MINOR,
    ZECTRIX_SDK_VERSION_PATCH,
};

}  // namespace v1
}  // namespace zectrix::sdk
