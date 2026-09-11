#include "zectrix_health_esp.h"

#include "zectrix_boot_esp.h"
#include "hal/wdt_hal.h"

namespace zectrix::system {

uint64_t EspHealthWatchdog::Milliseconds() const {
    return update::EspBootBackend{}.Milliseconds();
}

esp_err_t EspHealthWatchdog::Arm(uint32_t timeout_ms) {
    return update::EspBootBackend{}.ArmBootWatchdog(timeout_ms) == update::Result::kOk
        ? ESP_OK : ESP_FAIL;
}

void EspHealthWatchdog::Feed() {
    wdt_hal_context_t context = RWDT_HAL_CONTEXT_DEFAULT();
    wdt_hal_write_protect_disable(&context);
    wdt_hal_feed(&context);
    wdt_hal_write_protect_enable(&context);
}

void EspHealthWatchdog::Disarm() { update::EspBootBackend{}.DisarmBootWatchdog(); }

}  // namespace zectrix::system
