#pragma once

#include <cstdint>

#include "zectrix_canvas.h"

namespace zectrix::ui {

enum class RadioIndicator : uint8_t { Off, Ready, Connected, Active, Fault };

struct StatusBarState {
    bool time_valid = false;
    uint8_t hour = 0;
    uint8_t minute = 0;
    bool battery_valid = false;
    uint8_t battery_percent = 0;
    bool battery_absent = false;
    bool charging = false;
    bool charge_full = false;
    bool external_power = false;
    bool charge_fault = false;
    RadioIndicator ble = RadioIndicator::Off;
    RadioIndicator wifi = RadioIndicator::Off;

    bool operator==(const StatusBarState& other) const;
};

constexpr int kStatusBarHeight = 24;
void DrawStatusBar(ZectrixCanvas& canvas, const StatusBarState& state, bool inverted = false);

}  // namespace zectrix::ui
