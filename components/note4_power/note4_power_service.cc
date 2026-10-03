#include "note4_power_service.h"

#include <cstdlib>
#include <new>

#include "esp_sleep.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "note4_board.h"

namespace note4::power {
namespace { RTC_DATA_ATTR bool scheduled_sleep = false; }

esp_err_t PowerService::Attach(Note4Board& board, PowerService** out_service) {
    if (out_service == nullptr) return ESP_ERR_INVALID_ARG;
    *out_service = new (std::nothrow) PowerService(board);
    return *out_service == nullptr ? ESP_ERR_NO_MEM : ESP_OK;
}

PowerService::~PowerService() = default;

PowerSnapshot PowerService::ReadSnapshot() const {
    PowerSnapshot snapshot;
    if (board_ == nullptr) return snapshot;
    const Note4PowerSnapshot board_snapshot =
        board_->ReadPowerSnapshot();
    snapshot.battery_valid = board_snapshot.battery_valid;
    snapshot.battery_mv = board_snapshot.battery_mv;
    snapshot.battery_percent = board_snapshot.battery_percent;
    snapshot.external_power_present = board_snapshot.charge.power_present;
    snapshot.charging = board_snapshot.charge.charging;
    snapshot.charge_full = board_snapshot.charge.full;
    snapshot.charge_fault = board_snapshot.charge.fault;
    snapshot.battery_absent = board_snapshot.charge.no_battery;
    cached_ = snapshot;
    sampled_us_ = esp_timer_get_time();
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

bool PowerService::IsScheduledWake() const {
    return scheduled_sleep && GetWakeReason() == WakeReason::Timer;
}

[[noreturn]] void PowerService::Shutdown(void (*ready)(void*), void* context, uint64_t wake_after_us) {
    scheduled_sleep = false;
    bool button_ready = false;
    if (board_ != nullptr) {
        board_->ShutdownPeripherals();
        board_->SetPowerLed(false);
        board_->SetAudioPower(false);
        vTaskDelay(pdMS_TO_TICKS(100));
        const auto wake = board_->PreparePowerButtonWake();
        button_ready = wake == ESP_OK;
        if (wake != ESP_OK) ESP_LOGW("note4_power", "power-button wake unavailable: %s", esp_err_to_name(wake));
    }
    if (ready != nullptr) ready(context);
    if (wake_after_us != 0 && button_ready) {
        const auto timer = esp_sleep_enable_timer_wakeup(wake_after_us);
        scheduled_sleep = timer == ESP_OK;
        if (!scheduled_sleep) ESP_LOGW("note4_power", "scheduled wake unavailable: %s", esp_err_to_name(timer));
    }
    if (board_ != nullptr && !scheduled_sleep) {
        board_->CutBatteryPower();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    esp_deep_sleep_start();
    abort();
}

}  // namespace note4::power
