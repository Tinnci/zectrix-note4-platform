#include "ui_engine.h"
#include "layout.h"
#include "zectrix_locale.h"
#include "zectrix_utilities.h"

#include <cstdio>

using zectrix::i18n::Text;
using zectrix::i18n::Tr;
using namespace zectrix::app;

namespace {
Text TimerState(const FocusTimer& timer) {
    switch (timer.state()) {
        case FocusTimer::State::Ready: return Text::FocusReady;
        case FocusTimer::State::Running: return Text::FocusRunning;
        case FocusTimer::State::Paused: return Text::FocusPaused;
        case FocusTimer::State::Finished: return Text::FocusFinished;
    }
    return Text::None;
}

Text TimerControls(const FocusTimer& timer) {
    switch (timer.state()) {
        case FocusTimer::State::Ready: return Text::NavFocusStart;
        case FocusTimer::State::Running: return Text::NavFocusPause;
        case FocusTimer::State::Paused: return Text::NavFocusResume;
        case FocusTimer::State::Finished: return Text::NavFocusNext;
    }
    return Text::NavBackOff;
}
}  // namespace

esp_err_t UiEngine::ShowUtilities(const UtilityController& utilities, bool full_refresh) {
    const auto& session = utilities.session();
    const auto& timer = session.timer;
    const auto phase = timer.phase() == FocusTimer::Phase::Focus ? Text::FocusPhase : Text::BreakPhase;
    char line[128];

    switch (utilities.page()) {
        case UtilityPage::Menu: {
            auto page = EnterPage(zectrix::ui::PageSpec().Title(Tr(Text::PocketTools)).Footer(Tr(Text::NavOpenBack)));
            const bool portrait = page.portrait();
            const int width = page.width(), height = page.height();
            constexpr Text names[] = {Text::FocusTimer, Text::OfflineCalendar, Text::Counter};
            for (std::size_t i = 0; i < std::size(names); ++i) {
                const int y = 54 + static_cast<int>(i) * (portrait ? 84 : 62);
                const bool chosen = i == utilities.selected();
                canvas_.FillRect(16, y, width - 32, 30, chosen);
                canvas_.Rect(16, y, width - 32, 30);
                canvas_.TextFitted(28, y + 7, Tr(names[i]), width - 56, chosen);
                if (i == 0) std::snprintf(line, sizeof(line), Tr(Text::FocusSummary),
                    Tr(phase), utilities.timer_minutes(), Tr(TimerState(timer)));
                else if (i == 1) std::snprintf(line, sizeof(line), "%s", Tr(Text::CalendarRange));
                else std::snprintf(line, sizeof(line), Tr(Text::UtilityCount), session.count);
                WrapText(16, y + 34, line, width - 32, 18, portrait ? 2 : 1);
            }
            WrapText(16, height - (portrait ? 96 : 54), Tr(Text::UtilityOffline), width - 32, 18, portrait ? 2 : 1);
            return page.Commit(full_refresh);
        }
        case UtilityPage::Focus: {
            auto page = EnterPage(zectrix::ui::PageSpec().Title(Tr(Text::FocusTimer)).Footer(Tr(TimerControls(timer))));
            const int width = page.width();
            const int dy = page.dy();
            canvas_.TextCentered(56 + dy, Tr(phase));
            canvas_.TextCentered(80 + dy, Tr(TimerState(timer)));
            std::snprintf(line, sizeof(line), "%u", utilities.timer_minutes());
            canvas_.TextCentered(111 + dy, line, 4);
            canvas_.TextCentered(186 + dy, Tr(Text::FocusMinutes));
            const auto detail = timer.state() == FocusTimer::State::Ready ? Text::FocusRange :
                timer.state() != FocusTimer::State::Finished ? Text::FocusSilent :
                timer.phase() == FocusTimer::Phase::Focus ? Text::FocusNextBreak : Text::FocusNextSession;
            WrapText(16, 215 + dy, Tr(detail), width - 32, 18, 2, true);
            WrapText(16, 246 + dy + (page.portrait() ? 18 : 0), Tr(Text::UtilityTemporary), width - 32, 18, 2, true);
            return page.Commit(full_refresh);
        }
        case UtilityPage::Calendar: {
            auto page = EnterPage(zectrix::ui::PageSpec().Title(Tr(Text::OfflineCalendar)).Footer(Tr(Text::NavCalendar)));
            const bool portrait = page.portrait();
            const int width = page.width(), height = page.height();
            const auto& month = session.month;
            std::snprintf(line, sizeof(line), "%04d - %02d", month.year(), month.month());
            canvas_.TextCentered(53, line, 2);
            constexpr Text weekdays[] = {Text::WeekMon, Text::WeekTue, Text::WeekWed, Text::WeekThu,
                Text::WeekFri, Text::WeekSat, Text::WeekSun};
            // Seven columns: 50 px pitch in landscape, 40 px in portrait.
            const int pitch = portrait ? 40 : 50, left = portrait ? 10 : 25, cell_width = portrait ? 34 : 42;
            const int row_pitch = portrait ? 34 : 22, top = portrait ? 127 : 113;
            for (int column = 0; column < 7; ++column) {
                const char* label = Tr(weekdays[column]);
                canvas_.Text(left + column * pitch + (cell_width - canvas_.TextWidth(label)) / 2, top - 22, label);
            }
            const auto& today = utilities.today();
            const int first = month.first_weekday();
            for (int day = 1; day <= month.days(); ++day) {
                const int cell = first + day - 1;
                const int x = left + cell % 7 * pitch, y = top + cell / 7 * row_pitch;
                const bool current = utilities.has_today() && month.year() == today.year &&
                    month.month() == today.month && day == today.day;
                if (current) canvas_.FillRect(x, y - 2, cell_width, portrait ? 26 : 21, true);
                std::snprintf(line, sizeof(line), "%d", day);
                canvas_.Text(x + (cell_width - canvas_.TextWidth(line)) / 2, y + (portrait ? 3 : 0), line, 1, current);
            }
            if (utilities.has_today()) std::snprintf(line, sizeof(line), Tr(Text::CalendarTodayDate),
                today.year, today.month, today.day);
            else std::snprintf(line, sizeof(line), "%s", Tr(Text::CalendarBrowseOnly));
            WrapText(16, height - (portrait ? 60 : 51), line, width - 32, 18, 1, true);
            return page.Commit(full_refresh);
        }
        case UtilityPage::CalendarOptions: {
            const char* items[] = {Tr(utilities.has_today() ? Text::CalendarToday : Text::CalendarSetClock),
                                    Tr(Text::CalendarJump)};
            return ShowMenu(Tr(Text::OfflineCalendar), items, std::size(items), utilities.selected(),
                            Tr(Text::NavOpenBack), full_refresh);
        }
        case UtilityPage::CalendarJump: {
            auto page = EnterPage(zectrix::ui::PageSpec().Title(Tr(Text::CalendarJump))
                .Footer(Tr(utilities.selected() == 0 ? Text::NavClockNext : Text::NavCalendarJump)));
            const int width = page.width();
            WrapText(16, 62, Tr(Text::CalendarRange), width - 32, 18, 2, true);
            for (unsigned i = 0; i < 2; ++i) {
                const int y = 99 + static_cast<int>(i) * 52;
                const bool chosen = utilities.selected() == i;
                canvas_.FillRect(24, y, width - 48, 36, chosen);
                canvas_.Rect(24, y, width - 48, 36);
                std::snprintf(line, sizeof(line), Tr(i == 0 ? Text::YearValue : Text::MonthValue),
                    i == 0 ? utilities.draft().year() : utilities.draft().month());
                canvas_.Text(40, y + 10, line, 1, chosen);
            }
            WrapText(16, 220, Tr(Text::CalendarKeepsClock), width - 32, 18, 2, true);
            return page.Commit(full_refresh);
        }
        case UtilityPage::Counter: {
            auto page = EnterPage(zectrix::ui::PageSpec().Title(Tr(Text::Counter))
                .Footer(Tr(session.count == 0 && session.undo_count ? Text::NavCounterUndo : Text::NavCounterReset)));
            const int width = page.width();
            const int dy = page.dy();
            WrapText(16, 59 + dy, Tr(Text::UtilityCountHint), width - 32, 18, 2, true);
            std::snprintf(line, sizeof(line), "%04u", session.count);
            canvas_.TextCentered(101 + dy, line, 6);
            std::snprintf(line, sizeof(line), Tr(Text::UtilityCount), session.count);
            canvas_.TextCentered(215 + dy, line);
            WrapText(16, 246 + dy, Tr(Text::UtilityTemporary), width - 32, 18, 2, true);
            return page.Commit(full_refresh);
        }
        default: return ESP_ERR_INVALID_STATE;
    }
    return full_refresh ? RefreshFull() : RefreshAuto();
}
