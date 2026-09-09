#pragma once

#include "zectrix_boot_guard.h"

namespace zectrix::update {

// Core ESP-IDF boot metadata and inherited RTC watchdog operations.
class EspBootBackend final : public BootBackend {
public:
    Result ReadBootInfo(BootInfo* info) override;
    uint64_t Milliseconds() const override;
    Result ArmBootWatchdog(uint32_t timeout_ms) override;
    void DisarmBootWatchdog() override;
    Result ConfirmRunningImage(const Partition& expected) override;
};

}  // namespace zectrix::update
