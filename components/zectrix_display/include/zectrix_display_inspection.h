#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "zectrix_display_state.h"

namespace zectrix::display {

inline constexpr std::size_t kFramebufferPreviewBytes = 64;
enum class RefreshKind : uint8_t { kNone, kFull1Bpp, kPartial1Bpp, kFull4Bpp };

struct DisplayInspection {
    State state;
    RefreshKind last_refresh = RefreshKind::kNone;
    uint32_t refresh_count = 0;
    uint32_t failed_refresh_count = 0;
    int32_t last_error = 0;
    uint64_t last_duration_us = 0;
    bool powered = false;
    bool batch_active = false;
    bool framebuffer_valid = false;
    uint8_t bits_per_pixel = 0;
    uint32_t framebuffer_bytes = 0;
    // A copied prefix of the last successful frame, never a caller pointer.
    std::array<uint8_t, kFramebufferPreviewBytes> preview{};
};

}  // namespace zectrix::display
