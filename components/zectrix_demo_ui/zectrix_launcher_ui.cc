#include "zectrix_demo_ui.h"

#include <algorithm>
#include <cstdio>

#include "zectrix_launcher_controller.h"
#include "zectrix_reading_overview.h"
#include "zectrix_unicode_text.h"

namespace {

using Icon = zectrix::app::ApplicationIcon;
using IconGlyph = std::array<uint16_t, 16>;

// Original 16x16 glyphs. Rows are MSB-first; set bits use the tile's ink color.
constexpr IconGlyph kAppIcon = {
    0x0000, 0x0000, 0x3e7c, 0x3e7c, 0x366c, 0x3e7c, 0x3e7c, 0x0000,
    0x0000, 0x3e7c, 0x3e7c, 0x366c, 0x3e7c, 0x3e7c, 0x0000, 0x0000,
};
constexpr IconGlyph kBookIcon = {
    0x0000, 0x0000, 0xfe3f, 0xff7f, 0xc1c3, 0xc083, 0xcc9b, 0xc083,
    0xc083, 0xcc9b, 0xc083, 0xc083, 0xfcbf, 0xffff, 0x0080, 0x0000,
};
constexpr IconGlyph kTransferIcon = {
    0x0000, 0x0000, 0x1818, 0x3c18, 0x3c18, 0x7e18, 0x1818, 0x1818,
    0x1818, 0x1818, 0x187e, 0x183c, 0x183c, 0x1818, 0x0000, 0x0000,
};
constexpr IconGlyph kClockIcon = {
    0x0000, 0x03c0, 0x0ff0, 0x1c38, 0x318c, 0x318c, 0x6186, 0x6186,
    0x61f6, 0x61f6, 0x300c, 0x300c, 0x1c38, 0x0ff0, 0x03c0, 0x0000,
};
constexpr IconGlyph kSleepIcon = {
    0x0000, 0x0200, 0x0c00, 0x1c00, 0x3c00, 0x3c00, 0x7c00, 0x7e00,
    0x7f00, 0x7f80, 0x3fc0, 0x3ffc, 0x1ff8, 0x0ff0, 0x03c0, 0x0000,
};
constexpr IconGlyph kSettingsIcon = {
    0x0000, 0x0f00, 0x79fe, 0x79fe, 0x0f00, 0x0000, 0x0078, 0x7fce,
    0x7fce, 0x0078, 0x0000, 0x0f00, 0x79fe, 0x79fe, 0x0f00, 0x0000,
};
constexpr IconGlyph kToolsIcon = {
    0x0000, 0x01e0, 0x03c0, 0x0780, 0x07c2, 0x07e6, 0x07fc, 0x03fc,
    0x07f0, 0x0fe0, 0x1f00, 0x3e00, 0x7c00, 0x5800, 0x7000, 0x0000,
};

const IconGlyph& GlyphForIcon(Icon icon) {
    switch (icon) {
        case Icon::Book: return kBookIcon;
        case Icon::Transfer: return kTransferIcon;
        case Icon::Clock: return kClockIcon;
        case Icon::Sleep: return kSleepIcon;
        case Icon::Settings: return kSettingsIcon;
        case Icon::Tools: return kToolsIcon;
        case Icon::App: return kAppIcon;
    }
    return kAppIcon;
}

void DrawIcon(ZectrixCanvas& canvas, zectrix::app::ApplicationIcon icon, int x, int y, bool black) {
    const auto& glyph = GlyphForIcon(icon);
    for (int row = 0; row < 16; ++row) {
        for (int col = 0; col < 16; ++col) {
            if (glyph[row] & (0x8000U >> col)) canvas.Pixel(x + col, y + row, black);
        }
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
    DrawFrame("", footer);
    canvas_.TextCentered(26, "HOME", 1, true);
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
            DrawIcon(canvas_, entry.icon, x + 12, y + (height - 16) / 2, !active);
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
