#pragma once

#include "zectrix_health_supervisor.h"

namespace zectrix::system {

// Uses the RTC watchdog only after BootGuard has released trial protection.
class EspHealthWatchdog final : public HealthWatchdog {
public:
    uint64_t Milliseconds() const override;
    esp_err_t Arm(uint32_t timeout_ms) override;
    void Feed() override;
    void Disarm() override;
};

}  // namespace zectrix::system
