#pragma once

#include "freertos/FreeRTOS.h"

#include <ctime>

enum class ZectrixButton { kUp, kDown, kOk };
enum class ZectrixButtonAction { kClick, kLongPress };

struct ZectrixButtonEvent {
    ZectrixButton button = ZectrixButton::kOk;
    ZectrixButtonAction action = ZectrixButtonAction::kClick;
};

class ZectrixBoard {
public:
    bool WaitButton(ZectrixButtonEvent* event, TickType_t timeout_ticks) {
        last_timeout = timeout_ticks;
        if (!has_event || event == nullptr) return false;
        *event = next_event;
        has_event = false;
        return true;
    }

    void DrainButtons() { drained = true; }

    bool HasRtc() const { return rtc_available; }
    bool HasNfc() const { return nfc_available; }
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
    bool IsRtcTimerFired() { return rtc_available && timer_flag; }
    bool IsRtcInterruptActive() const {
        return rtc_available && rtc_interrupt_active;
    }

    ZectrixButtonEvent next_event;
    TickType_t last_timeout = 0;
    bool has_event = false;
    bool drained = false;
    bool rtc_available = true;
    bool nfc_available = true;
    bool rtc_io_ok = true;
    bool timer_flag = false;
    bool rtc_interrupt_active = false;
    std::uint8_t countdown_seconds = 0;
    tm rtc_value = {};
};
