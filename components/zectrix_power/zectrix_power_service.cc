#include "zectrix_power_service.h"

#include <cstdlib>

#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "zectrix_board.h"

namespace zectrix::power {

esp_err_t PowerService::Attach(void* board, PowerService** out_service) {
    if (board == nullptr || out_service == nullptr) return ESP_ERR_INVALID_ARG;
    *out_service = new PowerService(board);
    return ESP_OK;
}

PowerService::~PowerService() = default;

PowerSnapshot PowerService::ReadSnapshot() const {
    PowerSnapshot snapshot;
    if (board_ == nullptr) return snapshot;
    const ZectrixPowerSnapshot board_snapshot =
        static_cast<ZectrixBoard*>(board_)->ReadPowerSnapshot();
    snapshot.battery_valid = board_snapshot.battery_valid;
    snapshot.battery_mv = board_snapshot.battery_mv;
    snapshot.battery_percent = board_snapshot.battery_percent;
    snapshot.external_power_present = board_snapshot.charge.power_present;
    snapshot.charging = board_snapshot.charge.charging;
    return snapshot;
}

WakeReason PowerService::GetWakeReason() const {
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_UNDEFINED: return WakeReason::PowerOn;
        case ESP_SLEEP_WAKEUP_EXT0:
        case ESP_SLEEP_WAKEUP_EXT1: return WakeReason::ExternalPin;
        case ESP_SLEEP_WAKEUP_TIMER: return WakeReason::Timer;
        case ESP_SLEEP_WAKEUP_TOUCHPAD: return WakeReason::Touch;
        case ESP_SLEEP_WAKEUP_ULP: return WakeReason::ULP;
        default: return WakeReason::Other;
    }
}

[[noreturn]] void PowerService::Shutdown() {
    if (board_ != nullptr) {
        auto* board = static_cast<ZectrixBoard*>(board_);
        board->SetPowerLed(false);
        board->SetAudioPower(false);
        vTaskDelay(pdMS_TO_TICKS(100));
        board->CutBatteryPower();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    esp_deep_sleep_start();
    abort();
}

}  // namespace zectrix::power
