#include "zectrix_demo_ui.h"

#include <algorithm>
#include <cstdio>

#include "zectrix_launcher_controller.h"
#include "zectrix_reading_overview.h"
#include "zectrix_unicode_text.h"

namespace {

void DrawIcon(ZectrixCanvas& canvas, zectrix::app::ApplicationIcon icon, int x, int y, bool black) {
    using Icon = zectrix::app::ApplicationIcon;
    switch (icon) {
        case Icon::Book:
            canvas.Rect(x + 2, y + 3, 10, 18, black);
            canvas.Rect(x + 12, y + 3, 10, 18, black);
            canvas.Line(x + 5, y + 7, x + 8, y + 7, black);
            canvas.Line(x + 15, y + 7, x + 18, y + 7, black);
            break;
        case Icon::Transfer:
            canvas.Line(x + 7, y + 3, x + 7, y + 21, black);
            canvas.Line(x + 3, y + 7, x + 7, y + 3, black);
            canvas.Line(x + 11, y + 7, x + 7, y + 3, black);
            canvas.Line(x + 17, y + 3, x + 17, y + 21, black);
            canvas.Line(x + 13, y + 17, x + 17, y + 21, black);
            canvas.Line(x + 21, y + 17, x + 17, y + 21, black);
            break;
        case Icon::Clock:
        case Icon::Sleep:
            for (int row = 1; row < 23; ++row) {
                for (int col = 1; col < 23; ++col) {
                    const int radius = (row - 12) * (row - 12) + (col - 12) * (col - 12);
                    const int cutout = (row - 8) * (row - 8) + (col - 17) * (col - 17);
                    if (radius <= 100 && (icon == Icon::Clock ? radius >= 81 : cutout > 100))
                        canvas.Pixel(x + col, y + row, black);
                }
            }
            if (icon == Icon::Clock) {
                canvas.Line(x + 12, y + 6, x + 12, y + 12, black);
                canvas.Line(x + 12, y + 12, x + 17, y + 15, black);
            }
            break;
        case Icon::Settings:
            for (int row = 0; row < 3; ++row) {
                canvas.Line(x + 2, y + 5 + row * 7, x + 21, y + 5 + row * 7, black);
                canvas.Rect(x + (row == 1 ? 14 : 5), y + 3 + row * 7, 5, 5, black);
            }
            break;
        case Icon::App:
        case Icon::Tools:
            for (int row = 0; row < 2; ++row)
                for (int col = 0; col < 2; ++col)
                    canvas.Rect(x + 3 + col * 11, y + 3 + row * 11, 8, 8, black);
            break;
    }
}

void DrawOverview(ZectrixCanvas& canvas, const zectrix::time::ClockSnapshot& clock,
                  const zectrix::app::ReadingOverview& reading, bool active) {
    canvas.Rect(12, 52, 376, 78);
    canvas.Line(126, 60, 126, 121);
    if (active) canvas.FillRect(127, 53, 260, 76, true);
    if (clock.source != zectrix::time::ClockSource::Uptime && zectrix::time::IsValid(clock.value)) {
        constexpr const char* months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
            "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
        constexpr const char* weekdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
        const auto& date = clock.value;
        const auto weekday = (zectrix::time::CalendarSeconds(date) / 86400 + 4) % 7;
        canvas.Text(24, 58, weekdays[weekday]);
        char value[12];
        std::snprintf(value, sizeof(value), "%02d", date.day);
        canvas.Text(24, 78, value, 2);
        canvas.Text(78, 79, months[date.month - 1]);
        std::snprintf(value, sizeof(value), "%04d", date.year);
        canvas.Text(78, 102, value);
    } else {
        canvas.Text(24, 63, "DATE");
        canvas.Text(24, 84, "NOT SET");
        canvas.Text(24, 106, "Use CLOCK");
    }

    using State = zectrix::app::ReadingOverview::State;
    switch (reading.state) {
        case State::Saved: {
            canvas.Text(140, 58, "CONTINUE READING", 1, active);
            auto title = reading.book_id;
            title.back() = '\0';
            zectrix::ui::DrawUtf8Line(canvas, 140, 80, title.data(), 236, active);
            const unsigned progress = std::min<unsigned>(reading.progress_per_mille, 1000);
            char value[24];
            std::snprintf(value, sizeof(value), "%u.%u%%", progress / 10, progress % 10);
            const int label_x = 376 - canvas.TextWidth(value);
            canvas.Text(label_x, 103, value, 1, active);
            const int bar_width = label_x - 150;
            canvas.Rect(140, 109, bar_width, 6, !active);
            canvas.FillRect(142, 111, static_cast<int>((bar_width - 4) * progress / 1000), 2, !active);
            break;
        }
        case State::Empty:
            canvas.Text(140, 58, "OPEN LIBRARY", 1, active);
            canvas.Text(140, 83, "Choose your first book", 1, active);
            canvas.Text(140, 105, "TXT / EPUB", 1, active);
            break;
        case State::Error:
            canvas.Text(140, 58, "OPEN LIBRARY", 1, active);
            canvas.Text(140, 83, "Progress unavailable", 1, active);
            canvas.Text(140, 105, "OK to retry", 1, active);
            break;
        case State::Unavailable:
            canvas.Text(140, 58, "POCKET TERMINAL");
            canvas.Text(140, 83, "Clock, covers and tools");
            canvas.Text(140, 105, "Choose an app below");
            break;
    }
}

}  // namespace

esp_err_t ZectrixDemoUi::ShowLauncher(const zectrix::app::LauncherController& launcher,
                                     const zectrix::time::ClockSnapshot& clock,
                                     const zectrix::app::ReadingOverview& reading, bool full_refresh) {
    using Scene = zectrix::app::LauncherScene;
    const auto count = launcher.count();
    if (launcher.scene() == Scene::Tools) {
        std::array<const char*, zectrix::app::ApplicationCatalog::kCapacity> labels{};
        for (std::size_t i = 0; i < count; ++i) labels[i] = launcher.EntryAt(i).label;
        return ShowMenu("TOOLS", labels.data(), count, launcher.selected(),
                        "UP/DOWN Move  OK Open  Hold OK Back", full_refresh);
    }
    if (launcher.scene() != Scene::Home) return ESP_ERR_INVALID_STATE;
    const char* footer = "UP/DOWN Move  OK Open  Hold DOWN Off";
    if (launcher.overview_selected()) footer = reading.state == zectrix::app::ReadingOverview::State::Saved
        ? "UP/DOWN Move  OK Read  Hold DOWN Off" : "UP/DOWN Move  OK Library  Hold DOWN Off";
    DrawFrame("ZECTRIX | HOME", footer);
    DrawOverview(canvas_, clock, reading, launcher.overview_selected());
    const auto tiles = count - launcher.tile_offset();
    if (tiles == 0) {
        canvas_.TextCentered(188, "NO APPS AVAILABLE");
    } else {
        constexpr auto per_page = zectrix::app::LauncherController::kTilesPerPage;
        const auto page = launcher.tile_page();
        const auto first = launcher.tile_offset() + page * per_page;
        const auto visible = std::min(per_page, count - first);
        const int rows = static_cast<int>((visible + 1) / 2);
        const int height = (126 - (rows - 1) * 6) / rows;
        for (std::size_t slot = 0; slot < visible; ++slot) {
            const auto index = first + slot;
            const auto entry = launcher.EntryAt(index);
            const int x = 12 + static_cast<int>(slot % 2) * 192;
            const int y = 138 + static_cast<int>(slot / 2) * (height + 6);
            const bool active = index == launcher.selected();
            canvas_.FillRect(x, y, 184, height, active);
            canvas_.Rect(x, y, 184, height);
            DrawIcon(canvas_, entry.icon, x + 8, y + (height - 24) / 2, !active);
            DrawFittedText(canvas_, x + 40, y + (height - 16) / 2, entry.label, 136, active);
        }
        if (tiles > per_page) {
            char pages[16];
            std::snprintf(pages, sizeof(pages), "%u/%u", static_cast<unsigned>(page + 1),
                          static_cast<unsigned>((tiles + per_page - 1) / per_page));
            canvas_.Text(388 - canvas_.TextWidth(pages), 26, pages, 1, true);
        }
    }
    return full_refresh ? RefreshFull() : RefreshAuto();
}
