#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "zectrix_display_state.h"
#include "zectrix_display_telemetry.h"

namespace zectrix::display {

inline constexpr std::size_t kFramebufferPreviewBytes = 64;

struct DisplayInspection {
    State state;
    RefreshKind last_refresh = RefreshKind::kNone;
    uint32_t refresh_count = 0;
    uint32_t failed_refresh_count = 0;
    int32_t last_error = 0;
    uint64_t last_duration_us = 0;
    uint32_t debt_mean_q16 = 0, debt_peak_q16 = 0;
    uint32_t global_limit_q16 = 0, local_limit_q16 = 0, model_revision = 0;
    RefreshReason last_reason = RefreshReason::Recovery;
    bool powered = false;
    bool batch_active = false;
    bool framebuffer_valid = false;
    uint8_t bits_per_pixel = 0;
    uint32_t framebuffer_bytes = 0;
    // A copied prefix of the last successful frame, never a caller pointer.
    std::array<uint8_t, kFramebufferPreviewBytes> preview{};
};

}  // namespace zectrix::display
