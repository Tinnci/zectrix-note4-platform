#pragma once

#include <cstdint>

#include "esp_err.h"

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
};

class PowerService {
public:
    // Attaches to initialized board support. The service does not own it.
    static esp_err_t Attach(void* board, PowerService** out_service);
    ~PowerService();

    PowerService(const PowerService&) = delete;
    PowerService& operator=(const PowerService&) = delete;

    PowerSnapshot ReadSnapshot() const;
    WakeReason GetWakeReason() const;

    // Turns off board rails and enters deep sleep. This function does not return.
    [[noreturn]] void Shutdown();

private:
    explicit PowerService(void* board) : board_(board) {}
    void* board_;
};

}  // namespace zectrix::power
