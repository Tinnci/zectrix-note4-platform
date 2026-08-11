#pragma once

#include "freertos/FreeRTOS.h"

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

    ZectrixButtonEvent next_event;
    TickType_t last_timeout = 0;
    bool has_event = false;
    bool drained = false;
};
