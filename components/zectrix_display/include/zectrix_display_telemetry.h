#pragma once

#include "zectrix_display_physics.h"

namespace zectrix::display {

enum TelemetryFlags : uint16_t {
    TransitionsKnown = 1, DriverMetricsKnown = 2, PanelTemperatureRead = 4,
    EnergyEstimated = 8, PowerBatch = 16,
};

struct FrameTelemetry {
    uint64_t sequence = 0, started_us = 0;
    uint32_t duration_us = 0, busy_us = 0, refresh_busy_us = 0;
    uint32_t spi_bytes = 0, ram_bytes = 0;
    uint32_t black_to_white = 0, white_to_black = 0;
    Rect window;
    PhysicsEnvironment environment;
    uint32_t model_revision = 0;
    uint32_t projected_mean_q16 = 0, projected_peak_q16 = 0;
    uint32_t committed_mean_q16 = 0, committed_peak_q16 = 0;
    uint32_t energy_uj = 0;
    int32_t error = 0;
    int16_t panel_temperature_centi_c = 0;
    uint16_t gain_q8 = 256, flags = 0;
    uint8_t waveform_triggers = 0;
    RefreshKind kind = RefreshKind::kNone;
    RefreshReason reason = RefreshReason::Recovery;
};

inline constexpr std::size_t kTelemetryCapacity = 16, kTelemetryBatchSize = 4;
struct TelemetryBatch {
    std::array<FrameTelemetry, kTelemetryBatchSize> frames{};
    uint64_t next = 0, lost = 0, latest = 0;
    uint8_t count = 0;
};

// Single-owner recorder: no locks, atomics, allocation, formatting or callbacks.
// CLI copies a bounded batch at the existing foreground dispatch safe point.
class TelemetryRecorder {
public:
    void Record(FrameTelemetry frame) {
        frame.sequence = ++latest_;
        frames_[(latest_ - 1) % kTelemetryCapacity] = frame;
    }
    TelemetryBatch Read(uint64_t after = 0) const {
        TelemetryBatch batch;
        batch.latest = latest_;
        // A future cursor can be left over from the previous boot.
        batch.next = after > latest_ ? 0 : after;
        const uint64_t oldest = latest_ >= kTelemetryCapacity ? latest_ - kTelemetryCapacity + 1 : 1;
        if (batch.next < oldest - 1) {
            batch.lost = oldest - 1 - batch.next;
            batch.next = oldest - 1;
        }
        while (batch.next < latest_ && batch.count < batch.frames.size()) {
            batch.frames[batch.count++] = frames_[batch.next % kTelemetryCapacity];
            ++batch.next;
        }
        return batch;
    }

private:
    std::array<FrameTelemetry, kTelemetryCapacity> frames_{};
    uint64_t latest_ = 0;
};

}  // namespace zectrix::display
