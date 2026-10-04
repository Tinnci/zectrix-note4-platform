#include "note4_locale.h"
#include "ui_engine.h"
#include "layout.h"
#include "focus_indicator.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "note4_launcher_controller.h"
#include "note4_reading_overview.h"
#include "unicode_text.h"

using note4::i18n::Tr;
using note4::i18n::Text;

namespace {
const char* EntryStatus(note4::app::LauncherEntryState state) {
    using State = note4::app::LauncherEntryState;
    return state == State::Opening ? Tr(Text::LauncherOpening) :
        state == State::Unavailable ? Tr(Text::LauncherUnavailable) : nullptr;
}

using Icon = note4::app::ApplicationIcon;
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

void DrawIcon(Canvas& canvas, note4::app::ApplicationIcon icon, int x, int y, int side, bool black) {
    const auto& glyph = GlyphForIcon(icon);
    for (int row = 0; row < side; ++row) {
        for (int col = 0; col < side; ++col) {
            if (glyph[row * 16 / side] & (0x8000U >> (col * 16 / side)))
                canvas.Pixel(x + col, y + row, black);
        }
    }
}

// The same editorial geometry is used in both orientations. Only its width changes.
void DrawOverview(Canvas& canvas, const note4::time::ClockSnapshot& clock,
                  const note4::app::ReadingOverview& reading, bool active) {
    constexpr int date_x = 16, divider_x = 84, reading_x = 96;
    const int right = canvas.width() - 16;
    const int text_width = right - reading_x - 8;
    canvas.Line(divider_x, 54, divider_x, 122);
    note4::ui::DrawFocusRail(canvas, {92, 52, right - 92, 74}, active);

    if (clock.source != note4::time::ClockSource::Uptime && note4::time::IsValid(clock.value)) {
        constexpr Text months[] = {Text::Jan, Text::Feb, Text::Mar, Text::Apr, Text::May, Text::Jun,
            Text::Jul, Text::Aug, Text::Sep, Text::Oct, Text::Nov, Text::Dec};
        constexpr Text weekdays[] = {Text::Sun, Text::Mon, Text::Tue, Text::Wed, Text::Thu, Text::Fri, Text::Sat};
        const auto& date = clock.value;
        const auto weekday = (note4::time::CalendarSeconds(date) / 86400 + 4) % 7;
        char value[32];
        std::snprintf(value, sizeof(value), Tr(Text::HomeDate), Tr(months[date.month - 1]), date.day);
        canvas.UiText(date_x, 62, value, 64, Canvas::UiFace::Compact);
        canvas.UiText(date_x, 84, Tr(weekdays[weekday]), 64, Canvas::UiFace::Caption);
        std::snprintf(value, sizeof(value), "%04d", date.year);
        canvas.UiText(date_x, 108, value, 64, Canvas::UiFace::Caption);
    } else {
        canvas.UiText(date_x, 60, Tr(Text::Date), 64, Canvas::UiFace::Selected);
        canvas.UiText(date_x, 84, Tr(Text::NotSet), 64, Canvas::UiFace::Caption);
        canvas.UiText(date_x, 108, Tr(Text::UseClock), 64, Canvas::UiFace::Caption);
    }

    using State = note4::app::ReadingOverview::State;
    const Text heading = reading.state == State::Saved ? Text::ContinueReading :
        reading.state == State::Unavailable ? Text::PocketTerminal : Text::OpenLibrary;
    canvas.UiText(reading_x, 54, Tr(heading), text_width, Canvas::UiFace::Label);
    if (reading.state == State::Saved) {
        auto title = reading.book_id;
        title.back() = '\0';
        // The extension drops to a regular 18px face on the shared baseline (design: ".epub").
        const char* dot = std::strrchr(title.data(), '.');
        const char* ext = dot && dot != title.data() && dot[1] ? dot : nullptr;
        auto stem = title;
        if (ext) {
            std::memcpy(stem.data(), title.data(), static_cast<std::size_t>(ext - title.data()));
            stem[ext - title.data()] = '\0';
        }
        const int ext_width = ext ? canvas.UiTextWidth(ext, Canvas::UiFace::Navigation) : 0;
        if (ext && canvas.UiTextWidth(stem.data(), Canvas::UiFace::Heading) + ext_width <= text_width) {
            const int stem_width = canvas.UiTextWidth(stem.data(), Canvas::UiFace::Heading);
            canvas.UiText(reading_x, 78, stem.data(), stem_width, Canvas::UiFace::Heading);
            canvas.UiText(reading_x + stem_width, 82, ext, ext_width, Canvas::UiFace::Navigation);
        } else {
            canvas.UiText(reading_x, 78, title.data(), text_width, Canvas::UiFace::Heading);
        }
        const unsigned progress = std::min<unsigned>(reading.progress_per_mille, 1000);
        char value[24];
        std::snprintf(value, sizeof(value), "%u.%u%%", progress / 10, progress % 10);
        const int label_x = right - 8 - canvas.UiTextWidth(value, Canvas::UiFace::Caption);
        canvas.UiText(label_x, 108, value, right - label_x, Canvas::UiFace::Caption);
        const int bar_width = std::max(0, label_x - reading_x - 12);
        canvas.Line(reading_x, 114, reading_x + bar_width, 114);
        canvas.FillRect(reading_x, 112, static_cast<int>(bar_width * progress / 1000), 3, true);
    } else {
        const Text detail = reading.state == State::Empty ? Text::ChooseFirstBook :
            reading.state == State::Error ? Text::ProgressUnavailable : Text::ClockCoversTools;
        const char* hint = reading.state == State::Empty ? "TXT / EPUB" :
            Tr(reading.state == State::Error ? Text::OkRetry : Text::ChooseAppBelow);
        canvas.UiText(reading_x, 84, Tr(detail), text_width, Canvas::UiFace::Caption);
        canvas.UiText(reading_x, 108, hint, text_width, Canvas::UiFace::Caption);
    }

    // Ben-Day accent: bounded procedural ink, anchored to the logical screen.
    // It never follows focus, sits behind no text, and needs no bitmap or timer.
    // Staggered single-pixel stipple, denser than the earlier sparse rows (design: light band).
    for (int row = 0, y = 130; y < 152; y += 3, ++row)
        for (int x = 92 + (row & 1) * 2; x < right; x += 4)
            canvas.Pixel(x, y, true);
}
}  // namespace

esp_err_t UiEngine::ShowLauncher(const note4::app::LauncherController& launcher,
                                 const note4::time::ClockSnapshot& clock,
                                 const note4::app::ReadingOverview& reading, bool full_refresh) {
    using Scene = note4::app::LauncherScene;
    const auto count = launcher.count();
    if (launcher.scene() == Scene::Tools) {
        std::array<const char*, note4::app::ApplicationCatalog::kCapacity> labels{};
        std::array<UiEngine::MenuRowState, note4::app::ApplicationCatalog::kCapacity> states{};
        for (std::size_t i = 0; i < count; ++i) {
            const auto entry = launcher.EntryAt(i);
            labels[i] = Tr(entry.label_text, entry.label);
            states[i] = {EntryStatus(entry.state), entry.state != note4::app::LauncherEntryState::Unavailable};
        }
        return ShowMenu(Tr(Text::Tools), labels.data(), count, launcher.selected(),
                        Tr(Text::NavOpenBack), full_refresh, states.data());
    }
    if (launcher.scene() != Scene::Home) return ESP_ERR_INVALID_STATE;

    const char* footer = Tr(Text::NavOpenOff);
    if (launcher.overview_selected()) {
        footer = reading.state == note4::app::ReadingOverview::State::Saved
            ? Tr(Text::NavReadOff) : Tr(Text::NavLibraryOff);
    }

    constexpr auto per_page = note4::app::LauncherController::kTilesPerPage;
    const auto tiles = count - launcher.tile_offset();
    const auto page = launcher.tile_page();

    char pages_str[24] = {};
    if (tiles > per_page) {
        std::snprintf(pages_str, sizeof(pages_str), "%u/%u", static_cast<unsigned>(page + 1),
                      static_cast<unsigned>((tiles + per_page - 1) / per_page));
    }

    auto shell = EnterPage(note4::ui::PageSpec()
        .Title(Tr(Text::Home))
        .QuietTitle()
        .Footer(footer)
        .Badge(pages_str[0] ? pages_str : nullptr));

    DrawOverview(canvas_, clock, reading, launcher.overview_selected());

    if (tiles == 0) {
        canvas_.TextCentered(shell.portrait() ? 250 : 188, Tr(Text::NoApps));
    } else {
        const auto first = launcher.tile_offset() + page * per_page;
        const auto visible = std::min(per_page, count - first);

        // Responsive grid: 2 columns in landscape (width 376), 1 column in portrait (width 276).
        const int top_y = 156;
        const int area_h = FooterTop() - 6 - top_y;
        const int gap_y = shell.portrait() ? 0 : 6;
        const note4::ui::Rect tiles_area(16, top_y, shell.width() - 32, area_h);
        const auto grid = note4::ui::UniformGrid::Fit(
            tiles_area, static_cast<int>(per_page), 160, 8, gap_y);

        for (std::size_t slot = 0; slot < visible; ++slot) {
            const auto index = first + slot;
            const auto entry = launcher.EntryAt(index);
            const auto cell = grid.Cell(static_cast<int>(slot));
            const bool active = index == launcher.selected() && entry.state != note4::app::LauncherEntryState::Unavailable;
            const char* status = EntryStatus(entry.state);
            const int status_width = std::min(canvas_.UiTextWidth(status, Canvas::UiFace::Caption), cell.width / 3);

            note4::ui::DrawFocusRail(canvas_, cell, active);
            DrawIcon(canvas_, entry.icon, cell.x + 12, cell.y + (cell.height - 22) / 2, 22, true);
            canvas_.UiText(cell.x + 46, cell.y + (cell.height - 22) / 2,
                           Tr(entry.label_text, entry.label), cell.width - 54 - (status_width ? status_width + 12 : 0),
                           note4::ui::FocusTextFace(active));
            if (status_width) canvas_.UiText(cell.right() - 8 - status_width,
                cell.y + (cell.height - 18) / 2, status, status_width, Canvas::UiFace::Caption);
            canvas_.Line(cell.x + 12, cell.bottom() - 1, cell.right() - 1, cell.bottom() - 1);
        }
    }
    return shell.Commit(full_refresh);
}
