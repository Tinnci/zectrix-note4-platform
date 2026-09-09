#include "zectrix_demo_ui.h"
#include "zectrix_sleep_cover.h"
#include "zectrix_unicode_text.h"

#include <algorithm>
#include <cstdio>

namespace {
using namespace zectrix::app;
constexpr const char* kMonths[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
constexpr const char* kWeekdays[] = {"MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY", "SUNDAY"};

void DrawLandscape(ZectrixCanvas& canvas, unsigned variation) {
    const int sun_x = 265 + static_cast<int>(variation % 3) * 20;
    for (int y = -13; y <= 13; ++y) {
        for (int x = -13; x <= 13; ++x) {
            const int radius = x * x + y * y;
            if (radius <= 169 && radius >= 132) canvas.Pixel(sun_x + x, 65 + y, true);
        }
    }
    const int ridge[][2] = {{32, 133}, {86, 99}, {120, 118}, {185, 62}, {253, 121}, {294, 97}, {368, 133}};
    for (std::size_t i = 1; i < std::size(ridge); ++i)
        canvas.Line(ridge[i - 1][0], ridge[i - 1][1], ridge[i][0], ridge[i][1]);
    canvas.Line(185, 62, 177, 101);
    canvas.Line(177, 101, 198, 90);
    canvas.Line(198, 90, 216, 107);
    canvas.Line(32, 134, 368, 134);
    for (int row = 0; row < 4; ++row) {
        const int left = 64 + row * 23;
        canvas.Line(left, 141 + row * 7, 336 - row * 23, 141 + row * 7);
    }
}

void DrawCalendar(ZectrixCanvas& canvas, const SleepCoverSnapshot& snapshot, const SleepCalendar& calendar) {
    const auto& date = snapshot.clock.value;
    if (!calendar.valid) {
        canvas.TextCentered(58, "TIME NOT SET", 2);
        canvas.TextCentered(114, "Set the clock for a daily calendar.");
        canvas.TextCentered(147, "Your saved reading position is below.");
        return;
    }
    char line[40];
    std::snprintf(line, sizeof(line), "%s %04d", kMonths[date.month - 1], date.year);
    canvas.Text(16, 32, line);
    std::snprintf(line, sizeof(line), "%02d", date.day);
    canvas.Text(14, 55, line, 4);
    canvas.Text(16, 130, kWeekdays[calendar.weekday]);
    std::snprintf(line, sizeof(line), "AS OF %02d:%02d", date.hour, date.minute);
    canvas.Text(16, 152, line);
    const char* weekdays[] = {"M", "T", "W", "T", "F", "S", "S"};
    for (unsigned col = 0; col < 7; ++col) canvas.Text(195 + col * 28, 32, weekdays[col]);
    for (unsigned day = 1; day <= calendar.days; ++day) {
        const unsigned cell = calendar.first_weekday + day - 1;
        const int x = 186 + (cell % 7) * 28, y = 54 + (cell / 7) * 19;
        const bool today = static_cast<int>(day) == date.day;
        if (today) canvas.FillRect(x, y - 1, 26, 18, true);
        std::snprintf(line, sizeof(line), "%u", day);
        canvas.Text(x + (26 - canvas.TextWidth(line)) / 2, y, line, 1, today);
    }
}

void DrawReading(ZectrixCanvas& canvas, const SleepCoverSnapshot& snapshot) {
    canvas.Line(16, 175, 383, 175);
    canvas.Text(16, 183, "LAST SAVED READING");
    if (!snapshot.has_reading) {
        canvas.Text(16, 206, "Open a book and make a little time.");
        return;
    }
    const auto progress = std::min<uint16_t>(snapshot.reading.progress_per_mille, 1000);
    char label[24];
    std::snprintf(label, sizeof(label), "%u.%u%%", progress / 10, progress % 10);
    canvas.Text(384 - canvas.TextWidth(label), 183, label);
    auto title = snapshot.reading.book_id;
    title.back() = 0;
    zectrix::ui::DrawUtf8Line(canvas, 16, 205, title.data(), 368);
    canvas.Rect(16, 229, 368, 5);
    canvas.FillRect(17, 230, 366 * progress / 1000, 3, true);
}
}  // namespace

esp_err_t ZectrixDemoUi::ShowSleepCoverMenu(SleepCoverStyle selected, SleepCoverStyle active,
                                           const char* status, bool full_refresh) {
    DrawFrame("SLEEP COVER", "UP/DOWN Select  OK Preview  Hold OK Home");
    const char* styles[] = {"DAILY DASHBOARD", "QUIET LANDSCAPE", "BLANK / PRIVACY"};
    const char* details[] = {"Calendar, saved reading and a daily line", "A daily line with a mountain illustration", "A clean white screen after power-off"};
    for (unsigned i = 0; i < std::size(styles); ++i) {
        const bool chosen = i == static_cast<unsigned>(selected);
        const int y = 54 + i * 62;
        canvas_.FillRect(16, y, 368, 30, chosen);
        canvas_.Rect(16, y, 368, 30);
        canvas_.Text(28, y + 7, styles[i], 1, chosen);
        if (i == static_cast<unsigned>(active)) canvas_.Text(354, y + 7, "*", 1, chosen);
        canvas_.Text(16, y + 34, details[i]);
    }
    canvas_.Text(16, 246, status ? status : "OK sets your cover. Hold DOWN to sleep.");
    return full_refresh ? RefreshFull() : RefreshAuto();
}

esp_err_t ZectrixDemoUi::ShowSleepCover(const SleepCoverSnapshot& snapshot, SleepCoverStyle style,
                                       bool preview, bool preference_saved) {
    if (display_ == nullptr) return ESP_ERR_INVALID_STATE;
    style = SleepCoverSetting(static_cast<uint32_t>(style));
    if (!preview && style == SleepCoverStyle::Blank) return ClearDisplay();
    if (preview) BeginContent();
    else {
        gray_frame_.reset();
        canvas_.ResetClip();
        canvas_.Clear();
        canvas_.Text(16, 4, "NOTE4 / AT REST");
        char battery[24];
        if (snapshot.power.battery_valid && !snapshot.power.battery_absent)
            std::snprintf(battery, sizeof(battery), "BAT %u%%", std::min<unsigned>(snapshot.power.battery_percent, 100));
        else std::snprintf(battery, sizeof(battery), "BAT --");
        canvas_.Text(384 - canvas_.TextWidth(battery), 4, battery);
        canvas_.Line(16, 23, 383, 23);
    }
    const auto calendar = CalendarForSleep(snapshot.clock);
    const auto& quote = QuoteForSleep(calendar);
    if (style == SleepCoverStyle::Dashboard) {
        DrawCalendar(canvas_, snapshot, calendar);
        DrawReading(canvas_, snapshot);
        char line[96];
        std::snprintf(line, sizeof(line), "%s %s", quote.first, quote.second);
        zectrix::ui::DrawUtf8Line(canvas_, 16, 246, line, 368);
    } else if (style == SleepCoverStyle::Quote) {
        canvas_.TextCentered(31, "A MOMENT BETWEEN PAGES");
        DrawLandscape(canvas_, calendar.day_number);
        canvas_.TextCentered(180, quote.first);
        canvas_.TextCentered(205, quote.second);
        char date[48];
        const auto& value = snapshot.clock.value;
        if (calendar.valid) std::snprintf(date, sizeof(date), "AS OF %02d %s %04d  %02d:%02d",
            value.day, kMonths[value.month - 1], value.year, value.hour, value.minute);
        else std::snprintf(date, sizeof(date), "TIME NOT SET");
        canvas_.TextCentered(246, date);
    } else {
        canvas_.TextCentered(101, "A QUIET BLANK SCREEN");
        canvas_.TextCentered(152, "Book titles and dates stay private.");
        canvas_.TextCentered(207, "The display clears when you sleep.");
    }
    canvas_.Line(16, 273, 383, 273);
    canvas_.TextCentered(279, preview ? (preference_saved ? "PREVIEW   OK Sleep   Hold OK Back" :
        "NOT SAVED   OK Sleep   Hold OK Back") : "SLEEPING   Press DOWN to wake");
    if (preview) return RefreshFull();
    // Commit the final surface directly. Pending status invalidations stay dormant.
    sleep_surface_ = true;
    return display_->Present1Bpp(zectrix::display::DisplayIntent::FullClean, canvas_.data(), canvas_.size());
}
