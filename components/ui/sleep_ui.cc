#include "note4_locale.h"
#include "ui_engine.h"
#include "note4_sleep_cover.h"
#include "unicode_text.h"
#include "focus_indicator.h"
#include "sdkconfig.h"

#include <algorithm>
#include <cstdio>

using note4::i18n::Tr;
using note4::i18n::Text;

namespace {
using namespace note4::app;
constexpr Text kMonths[] = {Text::Jan, Text::Feb, Text::Mar, Text::Apr, Text::May, Text::Jun,
    Text::Jul, Text::Aug, Text::Sep, Text::Oct, Text::Nov, Text::Dec};
constexpr Text kWeekdays[] = {Text::Monday, Text::Tuesday, Text::Wednesday, Text::Thursday,
    Text::Friday, Text::Saturday, Text::Sunday};

// Both orientations keep Monday-first placement and a single, solid today marker.
void DrawMonthGrid(Canvas& canvas, const SleepCalendar& calendar, int today,
                   int left, int heading_y, int top, int column_width, int row_height) {
    constexpr Text weekdays[] = {Text::WeekMon, Text::WeekTue, Text::WeekWed, Text::WeekThu,
        Text::WeekFri, Text::WeekSat, Text::WeekSun};
    const int cell_width = column_width - 2;
    for (unsigned col = 0; col < 7; ++col) {
        const char* label = Tr(weekdays[col]);
        canvas.Text(left + col * column_width + (cell_width - canvas.TextWidth(label)) / 2,
                    heading_y, label);
    }
    char label[4];
    for (unsigned day = 1; day <= calendar.days; ++day) {
        const unsigned cell = calendar.first_weekday + day - 1;
        const int x = left + (cell % 7) * column_width;
        const int y = top + (cell / 7) * row_height;
        const bool active = static_cast<int>(day) == today;
        if (active) canvas.FillRect(x, y - 2, cell_width, 20, true);
        std::snprintf(label, sizeof(label), "%u", day);
        canvas.Text(x + (cell_width - canvas.TextWidth(label)) / 2, y, label, 1, active);
    }
}

void DrawLandscape(Canvas& canvas, unsigned variation) {
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

void DrawCalendar(Canvas& canvas, const SleepCoverSnapshot& snapshot, const SleepCalendar& calendar) {
    const auto& date = snapshot.clock.value;
    if (!calendar.valid) {
        canvas.TextCentered(58, Tr(Text::TimeNotSet), 2);
        canvas.TextCentered(114, Tr(Text::SetClockForCalendar));
#if CONFIG_NOTE4_ENABLE_READER
        canvas.TextCentered(147, Tr(Text::SavedReadingBelow));
#endif
        return;
    }
    char line[40];
    std::snprintf(line, sizeof(line), Tr(Text::MonthYear), date.year, Tr(kMonths[date.month - 1]));
    canvas.Text(16, 32, line);
    canvas.LargeNumber(16, 64, date.day);
    canvas.Text(16, 130, Tr(kWeekdays[calendar.weekday]));
    std::snprintf(line, sizeof(line), Tr(Text::AsOfTime), date.hour, date.minute);
    canvas.Text(16, 152, line);
    DrawMonthGrid(canvas, calendar, date.day, 186, 32, 54, 28, 20);
}

#if CONFIG_NOTE4_ENABLE_READER
void DrawReading(Canvas& canvas, const SleepCoverSnapshot& snapshot) {
    canvas.Line(16, 175, 383, 175);
    canvas.Text(16, 183, Tr(Text::LastSavedReading));
    if (!snapshot.has_reading) {
        canvas.Text(16, 206, Tr(Text::MakeReadingTime));
        return;
    }
    const auto progress = std::min<uint16_t>(snapshot.reading.progress_per_mille, 1000);
    char label[24];
    std::snprintf(label, sizeof(label), "%u.%u%%", progress / 10, progress % 10);
    canvas.Text(384 - canvas.TextWidth(label), 183, label);
    auto title = snapshot.reading.book_id;
    title.back() = 0;
    note4::ui::DrawUtf8Line(canvas, 16, 205, title.data(), 368);
    canvas.Rect(16, 229, 368, 5);
    canvas.FillRect(17, 230, 366 * progress / 1000, 3, true);
}
#endif
}  // namespace

esp_err_t UiEngine::ShowSleepCoverMenu(SleepCoverStyle selected, SleepCoverStyle active,
                                           const char* status, bool full_refresh) {
    DrawFrame(Tr(Text::SleepCover), Tr(Text::NavCoverPreview), true);
    const bool portrait = canvas_.portrait();
    const int width = canvas_.width(), height = canvas_.height();
    const char* styles[] = {Tr(Text::DailyDashboard), Tr(Text::QuietLandscape), Tr(Text::BlankPrivacy), Tr(Text::PhonePicture)};
    const char* details[] = {
#if CONFIG_NOTE4_ENABLE_READER
        Tr(Text::DashboardDetail),
#else
        Tr(Text::CalendarDetail),
#endif
        Tr(Text::LandscapeDetail), Tr(Text::BlankDetail), Tr(Text::PhonePictureDetail)};
    for (unsigned i = 0; i < kSleepCoverStyleCount; ++i) {
        const bool chosen = i == static_cast<unsigned>(selected);
        const int y = kSleepCoverStyleCount == 4 ? 54 + i * (portrait ? 68 : 47) : 54 + i * (portrait ? 84 : 62);
        note4::ui::DrawFocusRail(canvas_, {16, y, width - 32, 26}, chosen);
        canvas_.UiText(28, y + 4, styles[i], width - 80, note4::ui::FocusTextFace(chosen));
        if (i == static_cast<unsigned>(active)) canvas_.Text(width - 46, y + 5, "*");
        WrapText(16, y + 28, details[i], width - 32, 18, portrait ? 2 : 1);
    }
    WrapText(16, height - (portrait ? 62 : 54), status ? status : Tr(Text::SetCoverHint), width - 32, 18, 1);
    return full_refresh ? RefreshFull() : RefreshAuto();
}

esp_err_t UiEngine::ShowSleepCover(const SleepCoverSnapshot& snapshot, SleepCoverStyle style,
                                       bool preview, bool preference_saved, const SleepCoverImage* picture) {
    if (display_ == nullptr) return ESP_ERR_INVALID_STATE;
    style = SleepCoverSetting(static_cast<uint32_t>(style));
    if (sleep_portrait_ && style == SleepCoverStyle::Dashboard)
        return ShowPortraitCalendar(snapshot, preview, preference_saved);
    // Landscape covers draw on the 400 x 300 canvas even when a portrait screen came before.
    UseCanvasMode(false);
    if (!preview && style == SleepCoverStyle::Blank) return ClearDisplay();
    if (preview) BeginContent();
    else {
        gray_frame_.reset();
        canvas_.ResetClip();
        canvas_.Clear();
        canvas_.Text(16, 4, Tr(Text::AtRest));
        char battery[24];
        if (snapshot.power.battery_valid && !snapshot.power.battery_absent)
            std::snprintf(battery, sizeof(battery), Tr(Text::BatteryPercent), std::min<unsigned>(snapshot.power.battery_percent, 100));
        else std::snprintf(battery, sizeof(battery), "%s", Tr(Text::BatteryUnknown));
        canvas_.Text(384 - canvas_.TextWidth(battery), 4, battery);
        canvas_.Line(16, 23, 383, 23);
    }
    const auto calendar = CalendarForSleep(snapshot.clock);
    const auto& quote = QuoteForSleep(calendar);
    bool picture_loaded = false;
    if (style == SleepCoverStyle::Picture && picture && picture->read) {
        std::array<uint8_t, Canvas::kStride> row{};
        picture_loaded = true;
        for (int y = preview ? 24 : 0; y < 273; ++y) {
            if (!picture->read(picture->context, y * row.size(), row.data(), row.size())) {
                picture_loaded = false;
                break;
            }
            // PBM uses one for black; the display canvas uses one for white.
            for (std::size_t x = 0; x < row.size(); ++x) canvas_.data()[y * row.size() + x] = ~row[x];
        }
        if (!picture_loaded) return ESP_FAIL;
    }
    if (style == SleepCoverStyle::Dashboard) {
        DrawCalendar(canvas_, snapshot, calendar);
#if CONFIG_NOTE4_ENABLE_READER
        DrawReading(canvas_, snapshot);
#endif
        char line[96];
        std::snprintf(line, sizeof(line), "%s %s", Tr(quote.first_text, quote.first), Tr(quote.second_text, quote.second));
        note4::ui::DrawUtf8Line(canvas_, 16, 246, snapshot.weather_line[0] ? snapshot.weather_line.data() : line, 368);
    } else if (style == SleepCoverStyle::Quote) {
        canvas_.TextCentered(31, Tr(Text::BetweenPages));
        DrawLandscape(canvas_, calendar.day_number);
        canvas_.TextCentered(180, Tr(quote.first_text, quote.first));
        canvas_.TextCentered(205, Tr(quote.second_text, quote.second));
        char date[48];
        const auto& value = snapshot.clock.value;
        if (calendar.valid) std::snprintf(date, sizeof(date), Tr(Text::AsOfDate),
            value.year, value.month, value.day, value.hour, value.minute);
        else std::snprintf(date, sizeof(date), "%s", Tr(Text::TimeNotSet));
        canvas_.TextCentered(246, date);
    } else if (style == SleepCoverStyle::Picture) {
        if (!picture_loaded) {
            canvas_.TextCentered(92, Tr(Text::PhonePicture));
            canvas_.TextCentered(133, Tr(Text::PhonePictureMissing));
            canvas_.TextCentered(174, Tr(Text::PhonePictureDetail));
        }
    } else {
        canvas_.TextCentered(101, Tr(Text::QuietBlankScreen));
        canvas_.TextCentered(152, Tr(Text::ReadingStaysPrivate));
        canvas_.TextCentered(207, Tr(Text::DisplayClearsOnSleep));
    }
    if (preview) {
        canvas_.Line(16, 273, 383, 273);
        canvas_.TextCentered(279, preference_saved ? Tr(Text::PreviewControls) :
            Tr(Text::UnsavedPreviewControls));
        return RefreshFull();
    }
    canvas_.FillRect(16, 273, 368, 25, true);
    canvas_.TextCentered(278, Tr(Text::WakeHint), 1, true);
    // Commit the final surface directly. Pending status invalidations stay dormant.
    sleep_surface_ = true;
    return display_->Present1Bpp(note4::display::DisplayIntent::FullClean, canvas_.data(), canvas_.size());
}

esp_err_t UiEngine::ShowPortraitCalendar(const SleepCoverSnapshot& snapshot,
                                            bool preview, bool preference_saved) {
    gray_frame_.reset();
    UseCanvasMode(true);
    struct RestoreCanvas {
        UiEngine& ui;
        ~RestoreCanvas() { ui.UseCanvasMode(false); }
    } restore{*this};
    canvas_.Clear();
    const auto calendar = CalendarForSleep(snapshot.clock);
    canvas_.TextFitted(16, 12, Tr(Text::OfflineCalendar), 180, false, Canvas::TextStyle::Bold);
    char battery[24];
    if (snapshot.power.battery_valid && !snapshot.power.battery_absent)
        std::snprintf(battery, sizeof(battery), "%u%%", std::min<unsigned>(snapshot.power.battery_percent, 100));
    else std::snprintf(battery, sizeof(battery), "--%%");
    canvas_.Text(284 - canvas_.TextWidth(battery), 12, battery);
    canvas_.Line(16, 37, 283, 37);
    if (calendar.valid) {
        const auto& date = snapshot.clock.value;
        char label[48];
        std::snprintf(label, sizeof(label), Tr(Text::MonthYear), date.year, Tr(kMonths[date.month - 1]));
        canvas_.TextCentered(50, label);
        canvas_.LargeNumber((canvas_.width() - canvas_.LargeNumberWidth(date.day)) / 2, 82, date.day);
        canvas_.TextCentered(143, Tr(kWeekdays[calendar.weekday]));
        const unsigned weeks = (calendar.first_weekday + calendar.days + 6) / 7;
        // Six-week months still leave two complete lines for the daily sentence.
        DrawMonthGrid(canvas_, calendar, date.day, 17, 170, 190, 38, weeks == 6 ? 21 : 24);
        std::snprintf(label, sizeof(label), Tr(Text::AsOfTime), date.hour, date.minute);
        canvas_.TextCentered(358, label);
    } else {
        canvas_.TextCentered(100, Tr(Text::TimeNotSet), 2);
        canvas_.TextCentered(190, Tr(Text::SetClockForCalendar));
    }
    canvas_.Line(16, 354, 283, 354);
    if (snapshot.weather_line[0]) {
        canvas_.TextFitted(16, 334, snapshot.weather_line.data(), 268);
    } else {
        const auto& quote = QuoteForSleep(calendar);
        canvas_.TextCentered(316, Tr(quote.first_text, quote.first));
        canvas_.TextCentered(334, Tr(quote.second_text, quote.second));
    }
    canvas_.FillRect(0, 378, 300, 22, true);
    canvas_.TextFitted(8, 381, preview ? Tr(preference_saved ? Text::PreviewControls :
        Text::UnsavedPreviewControls) : Tr(Text::WakeHint), 284, true);
    // Also suppress landscape status redraws while viewing the portrait preview.
    sleep_surface_ = true;
    return display_->PresentPortrait1Bpp(canvas_.data(), canvas_.size());
}
