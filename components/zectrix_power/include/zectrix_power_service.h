#pragma once

#include <cstdint>

#include "esp_err.h"

class ZectrixBoard;

namespace zectrix::power {

enum class WakeReason : uint8_t {
    Unknown,
    PowerOn,
    ExternalPin,
    Timer,
    Touch,
    ULP,
    Other,
};

struct PowerSnapshot {
    bool battery_valid = false;
    uint16_t battery_mv = 0;
    uint8_t battery_percent = 0;
    bool external_power_present = false;
    bool charging = false;
    bool charge_full = false;
    bool charge_fault = false;
    bool battery_absent = false;
};

class PowerService {
public:
    // Attaches to initialized board support. The service does not own it.
    static esp_err_t Attach(ZectrixBoard& board, PowerService** out_service);
    ~PowerService();

    PowerService(const PowerService&) = delete;
    PowerService& operator=(const PowerService&) = delete;

    PowerSnapshot ReadSnapshot() const;
    // Foreground-owned cache; inspection must not start an ADC conversion.
    bool CachedSnapshot(PowerSnapshot* output, int64_t* sampled_us) const {
        if (!output || !sampled_us || sampled_us_ < 0) return false;
        *output = cached_;
        *sampled_us = sampled_us_;
        return true;
    }
    WakeReason GetWakeReason() const;

    // Platform stops peripheral consumers before calling this final transition.
    // Releases devices, arms a released power button, turns off rails and sleeps.
    [[noreturn]] void Shutdown();

private:
    explicit PowerService(ZectrixBoard& board) : board_(&board) {}
    ZectrixBoard* board_;
    mutable PowerSnapshot cached_{};
    mutable int64_t sampled_us_ = -1;
};

}  // namespace zectrix::power
