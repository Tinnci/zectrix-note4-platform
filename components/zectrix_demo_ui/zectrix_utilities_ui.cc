#include "zectrix_demo_ui.h"
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
}

esp_err_t ZectrixDemoUi::ShowUtilities(const UtilityController& utilities, bool full_refresh) {
    const auto& session = utilities.session();
    const auto& timer = session.timer;
    const auto phase = timer.phase() == FocusTimer::Phase::Focus ? Text::FocusPhase : Text::BreakPhase;
    char line[128];
    switch (utilities.page()) {
        case UtilityPage::Menu: {
            DrawFrame(Tr(Text::PocketTools), Tr(Text::NavOpenBack));
            constexpr Text names[] = {Text::FocusTimer, Text::OfflineCalendar, Text::Counter};
            for (std::size_t i = 0; i < std::size(names); ++i) {
                const int y = 54 + static_cast<int>(i) * 62;
                const bool chosen = i == utilities.selected();
                canvas_.FillRect(16, y, 368, 30, chosen);
                canvas_.Rect(16, y, 368, 30);
                canvas_.Text(28, y + 7, Tr(names[i]), 1, chosen);
                if (i == 0) std::snprintf(line, sizeof(line), Tr(Text::FocusSummary),
                    Tr(phase), utilities.timer_minutes(), Tr(TimerState(timer)));
                else if (i == 1) std::snprintf(line, sizeof(line), "%s", Tr(Text::CalendarRange));
                else std::snprintf(line, sizeof(line), Tr(Text::UtilityCount), session.count);
                canvas_.TextFitted(16, y + 34, line, 368);
            }
            canvas_.TextFitted(16, 246, Tr(Text::UtilityOffline), 368);
            break;
        }
        case UtilityPage::Focus: {
            DrawFrame(Tr(Text::FocusTimer), Tr(TimerControls(timer)));
            canvas_.TextCentered(56, Tr(phase));
            canvas_.TextCentered(80, Tr(TimerState(timer)));
            std::snprintf(line, sizeof(line), "%u", utilities.timer_minutes());
            canvas_.TextCentered(111, line, 4);
            canvas_.TextCentered(186, Tr(Text::FocusMinutes));
            const auto detail = timer.state() == FocusTimer::State::Ready ? Text::FocusRange :
                timer.state() != FocusTimer::State::Finished ? Text::FocusSilent :
                timer.phase() == FocusTimer::Phase::Focus ? Text::FocusNextBreak : Text::FocusNextSession;
            canvas_.TextCentered(215, Tr(detail));
            canvas_.TextCentered(246, Tr(Text::UtilityTemporary));
            break;
        }
        case UtilityPage::Calendar: {
            DrawFrame(Tr(Text::OfflineCalendar), Tr(Text::NavCalendar));
            const auto& month = session.month;
            std::snprintf(line, sizeof(line), "%04d - %02d", month.year(), month.month());
            canvas_.TextCentered(53, line, 2);
            constexpr Text weekdays[] = {Text::WeekMon, Text::WeekTue, Text::WeekWed, Text::WeekThu,
                Text::WeekFri, Text::WeekSat, Text::WeekSun};
            for (int column = 0; column < 7; ++column) {
                const char* label = Tr(weekdays[column]);
                canvas_.Text(25 + column * 50 + (42 - canvas_.TextWidth(label)) / 2, 91, label);
            }
            const auto& today = utilities.today();
            const int first = month.first_weekday();
            for (int day = 1; day <= month.days(); ++day) {
                const int cell = first + day - 1;
                const int x = 25 + cell % 7 * 50, y = 113 + cell / 7 * 22;
                const bool current = utilities.has_today() && month.year() == today.year &&
                    month.month() == today.month && day == today.day;
                if (current) canvas_.FillRect(x, y - 2, 42, 21, true);
                std::snprintf(line, sizeof(line), "%d", day);
                canvas_.Text(x + (42 - canvas_.TextWidth(line)) / 2, y, line, 1, current);
            }
            if (utilities.has_today()) std::snprintf(line, sizeof(line), Tr(Text::CalendarTodayDate),
                today.year, today.month, today.day);
            else std::snprintf(line, sizeof(line), "%s", Tr(Text::CalendarBrowseOnly));
            canvas_.TextCentered(249, line);
            break;
        }
        case UtilityPage::CalendarOptions: {
            const char* items[] = {Tr(utilities.has_today() ? Text::CalendarToday : Text::CalendarSetClock),
                                    Tr(Text::CalendarJump)};
            return ShowMenu(Tr(Text::OfflineCalendar), items, std::size(items), utilities.selected(),
                            Tr(Text::NavOpenBack), full_refresh);
        }
        case UtilityPage::CalendarJump: {
            DrawFrame(Tr(Text::CalendarJump), Tr(utilities.selected() == 0 ? Text::NavClockNext : Text::NavCalendarJump));
            canvas_.TextCentered(62, Tr(Text::CalendarRange));
            for (unsigned i = 0; i < 2; ++i) {
                const int y = 99 + static_cast<int>(i) * 52;
                const bool chosen = utilities.selected() == i;
                canvas_.FillRect(24, y, 352, 36, chosen);
                canvas_.Rect(24, y, 352, 36);
                std::snprintf(line, sizeof(line), Tr(i == 0 ? Text::YearValue : Text::MonthValue),
                    i == 0 ? utilities.draft().year() : utilities.draft().month());
                canvas_.Text(40, y + 10, line, 1, chosen);
            }
            canvas_.TextCentered(220, Tr(Text::CalendarKeepsClock));
            break;
        }
        case UtilityPage::Counter:
            DrawFrame(Tr(Text::Counter), Tr(session.count == 0 && session.undo_count ? Text::NavCounterUndo : Text::NavCounterReset));
            canvas_.TextCentered(59, Tr(Text::UtilityCountHint));
            std::snprintf(line, sizeof(line), "%04u", session.count);
            canvas_.TextCentered(101, line, 6);
            std::snprintf(line, sizeof(line), Tr(Text::UtilityCount), session.count);
            canvas_.TextCentered(215, line);
            canvas_.TextCentered(246, Tr(Text::UtilityTemporary));
            break;
        default: return ESP_ERR_INVALID_STATE;
    }
    return full_refresh ? RefreshFull() : RefreshAuto();
}
