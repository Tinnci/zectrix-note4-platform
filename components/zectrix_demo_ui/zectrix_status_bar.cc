#include "zectrix_locale.h"
#include "zectrix_status_bar.h"

#include <algorithm>
#include <cstdio>

using zectrix::i18n::Tr;
using zectrix::i18n::Text;

namespace zectrix::ui {

bool StatusBarState::operator==(const StatusBarState& o) const {
    return time_valid == o.time_valid && (!time_valid || (hour == o.hour && minute == o.minute)) &&
        battery_valid == o.battery_valid && (!battery_valid || battery_percent == o.battery_percent) &&
        charging == o.charging && external_power == o.external_power &&
        charge_fault == o.charge_fault && ble == o.ble && wifi == o.wifi;
}

namespace {
const char* RadioText(RadioIndicator state) {
    switch (state) {
        case RadioIndicator::Off: return Tr(Text::Off);
        case RadioIndicator::Ready: return Tr(Text::On);
        case RadioIndicator::Busy: return Tr(Text::Busy);
        case RadioIndicator::Connected: return Tr(Text::Link);
        case RadioIndicator::Fault: return Tr(Text::Error);
    }
    return "?";
}
}  // namespace

void DrawStatusBar(ZectrixCanvas& canvas, const StatusBarState& state) {
    canvas.FillRect(0, 0, 400, kStatusBarHeight, false);
    char text[12];
    if (state.time_valid) std::snprintf(text, sizeof(text), "%02u:%02u", state.hour, state.minute);
    else std::snprintf(text, sizeof(text), "--:--");
    canvas.Text(8, 4, text);

    // Draw radio symbols explicitly so the small font needs no icon glyphs.
    canvas.Line(85, 4, 85, 19);
    canvas.Line(85, 4, 92, 9);
    canvas.Line(92, 9, 79, 17);
    canvas.Line(79, 6, 92, 14);
    canvas.Line(92, 14, 85, 19);
    canvas.Text(100, 4, RadioText(state.ble));
    canvas.Line(170, 8, 178, 4);
    canvas.Line(178, 4, 186, 8);
    canvas.Line(173, 12, 178, 9);
    canvas.Line(178, 9, 183, 12);
    canvas.FillRect(177, 16, 3, 3, true);
    canvas.Text(194, 4, RadioText(state.wifi));

    if (state.charge_fault) canvas.Text(271, 4, "!");
    else if (state.charging) {
        canvas.Line(284, 3, 276, 12);
        canvas.Line(276, 12, 284, 12);
        canvas.Line(284, 12, 276, 21);
    } else if (state.external_power) canvas.Text(274, 4, "+");
    canvas.Rect(298, 6, 26, 13);
    canvas.FillRect(324, 10, 3, 5, true);
    if (state.battery_valid) {
        const unsigned percent = std::min<unsigned>(state.battery_percent, 100);
        canvas.FillRect(301, 9, static_cast<int>(20 * percent / 100), 7, true);
        std::snprintf(text, sizeof(text), "%u%%", percent);
    } else {
        canvas.Line(300, 8, 321, 16);
        std::snprintf(text, sizeof(text), "--%%");
    }
    canvas.Text(390 - canvas.TextWidth(text), 4, text);
    canvas.Line(0, kStatusBarHeight - 1, 399, kStatusBarHeight - 1);
}

}  // namespace zectrix::ui
