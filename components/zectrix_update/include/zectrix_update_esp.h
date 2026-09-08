#pragma once

#include "zectrix_update_service.h"

namespace zectrix::update {

// ESP-IDF OTA metadata and the inherited RTC watchdog stay below the service.
class EspUpdateBackend final : public UpdateBackend {
public:
    Result ReadBootInfo(BootInfo* info) override;
    uint64_t Milliseconds() const override;
    Result ArmBootWatchdog(uint32_t timeout_ms) override;
    void DisarmBootWatchdog() override;
    Result ConfirmRunningImage(const Partition& expected) override;
};

}  // namespace zectrix::update
