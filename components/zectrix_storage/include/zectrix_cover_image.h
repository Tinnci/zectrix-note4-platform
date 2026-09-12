#pragma once

#include <cstddef>
#include <cstdint>

namespace zectrix::storage::cover {

// Phone-side conversion keeps decoding and large image buffers off the device.
inline constexpr char kHeader[] = "P4\n400 300\n";
inline constexpr char kName[] = "phone.pbm";
constexpr std::size_t kHeaderSize = sizeof(kHeader) - 1;
constexpr uint32_t kPixelBytes = 400 * 300 / 8;
constexpr uint32_t kFileSize = kHeaderSize + kPixelBytes;

}  // namespace zectrix::storage::cover
