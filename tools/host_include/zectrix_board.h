#pragma once

#include "freertos/FreeRTOS.h"
#include "esp_err.h"

#include <ctime>

class ZectrixNfc;

enum class ZectrixButton { kUp, kDown, kOk };
enum class ZectrixButtonAction { kClick, kLongPress };

struct ZectrixButtonEvent {
    ZectrixButton button = ZectrixButton::kOk;
    ZectrixButtonAction action = ZectrixButtonAction::kClick;
};

struct ZectrixChargeSnapshot {
    bool power_present = false;
    bool charging = false;
    bool full = false;
    bool fault = false;
    bool no_battery = false;
};

struct ZectrixPowerSnapshot {
    bool battery_valid = false;
    std::uint16_t battery_mv = 0;
    std::uint8_t battery_percent = 0;
    ZectrixChargeSnapshot charge = {};
};

class ZectrixBoard {
public:
    esp_err_t Init();
    bool WaitButton(ZectrixButtonEvent* event, TickType_t timeout_ticks) {
        last_timeout = timeout_ticks;
        if (!has_event || event == nullptr) return false;
        *event = next_event;
        has_event = false;
        return true;
    }

    void DrainButtons() { drained = true; }

    ZectrixPowerSnapshot ReadPowerSnapshot() { return power_snapshot; }
    void SetPowerLed(bool on) {
        power_led = on;
        power_events[power_event_count++] = on ? 1 : 2;
    }
    void SetAudioPower(bool on) {
        audio_power = on;
        power_events[power_event_count++] = on ? 3 : 4;
    }
    void CutBatteryPower() { power_events[power_event_count++] = 5; }

    bool HasRtc() const { return rtc_available; }
    bool HasNfc() const { return nfc_available; }
    ZectrixNfc* nfc() const { return nullptr; }
    bool ReadRtc(tm* value) {
        if (!rtc_available || !rtc_io_ok || value == nullptr) return false;
        *value = rtc_value;
        return true;
    }
    bool WriteRtc(const tm& value) {
        if (!rtc_available || !rtc_io_ok) return false;
        rtc_value = value;
        return true;
    }
    bool StartRtcCountdown(std::uint8_t seconds) {
        countdown_seconds = seconds;
        return rtc_available && rtc_io_ok;
    }
    bool StopRtcCountdown() { return rtc_available && rtc_io_ok; }
    bool ClearRtcTimerFlag() {
        timer_flag = false;
        return rtc_available && rtc_io_ok;
    }
    esp_err_t ReadRtcTimerFlag(bool* fired) {
        if (fired == nullptr) return ESP_ERR_INVALID_ARG;
        if (!rtc_available) return ESP_ERR_NOT_FOUND;
        if (!rtc_io_ok) return ESP_FAIL;
        *fired = timer_flag;
        return ESP_OK;
    }
    bool IsRtcInterruptActive() const {
        return rtc_available && rtc_interrupt_active;
    }

    ZectrixButtonEvent next_event;
    TickType_t last_timeout = 0;
    bool has_event = false;
    bool drained = false;
    bool rtc_available = true;
    bool nfc_available = true;
    esp_err_t init_result = ESP_OK;
    bool rtc_io_ok = true;
    bool timer_flag = false;
    bool rtc_interrupt_active = false;
    std::uint8_t countdown_seconds = 0;
    tm rtc_value = {};
    ZectrixPowerSnapshot power_snapshot = {};
    bool power_led = true;
    bool audio_power = true;
    int power_events[8] = {};
    std::size_t power_event_count = 0;
};
