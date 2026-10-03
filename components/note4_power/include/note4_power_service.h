#pragma once

#include <cstdint>

#include "esp_err.h"

class Note4Board;

namespace note4::power {

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
    static esp_err_t Attach(Note4Board& board, PowerService** out_service);
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
    bool IsScheduledWake() const;

    // Platform stops peripheral consumers before calling this final transition.
    // Releases devices, arms a released power button, turns off rails and sleeps.
    // The optional owner hook runs after cleanup/wake setup, before cutting power.
    // A nonzero timer keeps the battery latch on for autonomous lock-screen
    // refresh. Timer setup failure falls back to the existing rail-off path.
    [[noreturn]] void Shutdown(void (*ready)(void*) = nullptr, void* context = nullptr,
                              uint64_t wake_after_us = 0);

private:
    explicit PowerService(Note4Board& board) : board_(&board) {}
    Note4Board* board_;
    mutable PowerSnapshot cached_{};
    mutable int64_t sampled_us_ = -1;
};

}  // namespace note4::power
