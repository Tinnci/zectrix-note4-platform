#include "zectrix_status_bar.h"

#include <algorithm>
#include <cstdio>

namespace zectrix::ui {
namespace {

// Original row masks, read from left to right. All icons share the same ink path.
constexpr uint16_t kBluetooth[] = {
    0b0001000, 0b0001100, 0b0001010, 0b1001001, 0b0101010, 0b0011100,
    0b0001000, 0b0011100, 0b0101010, 0b1001001, 0b0001010, 0b0001100, 0b0001000,
};
constexpr uint16_t kWifi[] = {
    0b0001111111000, 0b0110000000110, 0b1000000000001,
    0b0000111110000, 0b0001000001000, 0b0010000000100,
    0b0000000000000, 0b0000001000000, 0b0000011100000,
};

enum class Mark : uint8_t { None, Off, Ready, Connected, Active, Warning, Charging, Full, Plug, Unknown, Absent };
constexpr uint16_t kMarks[][7] = {
    {0, 0, 0, 0, 0, 0, 0},
    {0b00001, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b10000},
    {0, 0b01110, 0b10001, 0b10001, 0b10001, 0b01110, 0},
    {0, 0, 0b01110, 0b01110, 0b01110, 0, 0},
    {0b01000, 0b11100, 0b01000, 0b01010, 0b00010, 0b00111, 0b00010},
    {0b00100, 0b00100, 0b00100, 0b00100, 0, 0b00100, 0},
    {0b00010, 0b00110, 0b01100, 0b11111, 0b00110, 0b01100, 0b01000},
    {0, 0, 0b00001, 0b00010, 0b10100, 0b01000, 0},
    {0b01010, 0b01010, 0b11111, 0b10001, 0b01110, 0b00100, 0b00100},
    {0b01110, 0b10001, 0b00110, 0b00100, 0, 0b00100, 0},
    {0b10001, 0b01010, 0b00100, 0b00100, 0b01010, 0b10001, 0},
};

void Icon(ZectrixCanvas& canvas, int x, int y, int width, int height,
          const uint16_t* rows, bool ink) {
    for (int row = 0; row < height; ++row)
        for (int column = 0; column < width; ++column)
            if (rows[row] & (1U << (width - column - 1))) canvas.Pixel(x + column, y + row, ink);
}

void Badge(ZectrixCanvas& canvas, int x, int y, Mark mark, bool ink) {
    Icon(canvas, x, y, 5, 7, kMarks[static_cast<unsigned>(mark)], ink);
}

Mark RadioMark(RadioIndicator radio) {
    switch (radio) {
        case RadioIndicator::Off: return Mark::Off;
        case RadioIndicator::Ready: return Mark::Ready;
        case RadioIndicator::Connected: return Mark::Connected;
        case RadioIndicator::Active: return Mark::Active;
        case RadioIndicator::Fault: return Mark::Warning;
    }
    return Mark::Warning;
}

// A sentinel keeps unavailable measurements out of both fill and equality.
unsigned BatteryPercent(const StatusBarState& state) {
    return state.battery_valid && !state.battery_absent ?
        std::min<unsigned>(state.battery_percent, 100) : 101;
}

Mark PowerMark(const StatusBarState& state) {
    if (state.charge_fault) return Mark::Warning;
    if (!state.battery_absent && state.charge_full) return Mark::Full;
    if (!state.battery_absent && state.charging) return Mark::Charging;
    if (state.external_power) return Mark::Plug;
    return BatteryPercent(state) <= 10 ? Mark::Warning : Mark::None;
}

}  // namespace

bool StatusBarState::operator==(const StatusBarState& other) const {
    return time_valid == other.time_valid && (!time_valid || (hour == other.hour && minute == other.minute)) &&
        battery_absent == other.battery_absent && BatteryPercent(*this) == BatteryPercent(other) &&
        PowerMark(*this) == PowerMark(other) && RadioMark(ble) == RadioMark(other.ble) &&
        RadioMark(wifi) == RadioMark(other.wifi);
}

void DrawStatusBar(ZectrixCanvas& canvas, const StatusBarState& state, bool inverted) {
    const bool ink = !inverted;
    canvas.FillRect(0, 0, ZectrixCanvas::kWidth, kStatusBarHeight, inverted);
    char text[12];
    if (state.time_valid) std::snprintf(text, sizeof(text), "%02u:%02u", state.hour, state.minute);
    else std::snprintf(text, sizeof(text), "--:--");
    canvas.Text(8, 4, text, 1, inverted);

    // Fixed slots prevent radio and charge transitions from shifting neighbors.
    Icon(canvas, 260, 5, 7, 13, kBluetooth, ink);
    Badge(canvas, 272, 8, RadioMark(state.ble), ink);
    Icon(canvas, 288, 7, 13, 9, kWifi, ink);
    Badge(canvas, 304, 8, RadioMark(state.wifi), ink);
    Badge(canvas, 320, 8, PowerMark(state), ink);

    // A 20 x 10 silhouette leaves a one-pixel moat around five 2 x 6 cells.
    constexpr int x = 332, y = 7;
    canvas.Line(x + 1, y, x + 16, y, ink);
    canvas.Line(x + 1, y + 9, x + 16, y + 9, ink);
    canvas.Line(x, y + 1, x, y + 8, ink);
    canvas.Line(x + 17, y + 1, x + 17, y + 8, ink);
    canvas.FillRect(x + 18, y + 3, 2, 4, ink);
    const auto percent = BatteryPercent(state);
    if (percent <= 100) {
        const unsigned cells = (percent + 19) / 20;
        for (unsigned cell = 0; cell < cells; ++cell)
            canvas.FillRect(x + 2 + static_cast<int>(cell) * 3, y + 2, 2, 6, ink);
        std::snprintf(text, sizeof(text), "%u%%", percent);
    } else {
        Badge(canvas, x + 6, y + 2, state.battery_absent ? Mark::Absent : Mark::Unknown, ink);
        std::snprintf(text, sizeof(text), "--%%");
    }
    canvas.Text(392 - canvas.TextWidth(text), 4, text, 1, inverted);
    canvas.Line(0, kStatusBarHeight - 1, ZectrixCanvas::kWidth - 1, kStatusBarHeight - 1, ink);
}

}  // namespace zectrix::ui
