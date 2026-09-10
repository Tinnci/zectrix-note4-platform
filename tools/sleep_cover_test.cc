#include "zectrix_sleep_cover.h"

#include <cassert>
#include <climits>
#include <cstdio>
#include <cstring>

using namespace zectrix;
using namespace zectrix::app;

int main() {
    assert(SleepCoverSetting(0) == SleepCoverStyle::Dashboard);
    assert(SleepCoverSetting(1) == SleepCoverStyle::Quote);
    assert(SleepCoverSetting(2) == SleepCoverStyle::Blank);
    assert(SleepCoverSetting(UINT32_MAX) == kSleepCoverDefault);

    time::ClockSnapshot clock{{2026, 9, 9, 6, 20, 27, 0}, time::ClockSource::Rtc};
    auto calendar = CalendarForSleep(clock);
    assert(calendar.valid && calendar.days == 30 && calendar.first_weekday == 1 && calendar.weekday == 2);
    const auto day = calendar.day_number;
    const auto* quote = &QuoteForSleep(calendar);
    clock.value.hour = 23; clock.value.minute = 59;
    assert(CalendarForSleep(clock).day_number == day && &QuoteForSleep(CalendarForSleep(clock)) == quote);
    ++clock.value.day;
    assert(CalendarForSleep(clock).day_number == day + 1 && &QuoteForSleep(CalendarForSleep(clock)) != quote);
    clock = {{2024, 2, 29, 0, 12, 0, 0}, time::ClockSource::System};
    calendar = CalendarForSleep(clock);
    assert(calendar.valid && calendar.days == 29 && calendar.weekday == 3);
    clock.value = {2000, 2, 29, 0, 0, 0, 0};
    assert(CalendarForSleep(clock).valid && CalendarForSleep(clock).weekday == 1);
    clock.value = {2023, 1, 31, 0, 0, 0, 0};
    calendar = CalendarForSleep(clock);
    assert(calendar.days == 31 && calendar.first_weekday == 6 && calendar.weekday == 1);
    for (const auto date : {time::DateTime{2023, 2, 29, 0, 0, 0, 0}, {2000, 0, 1, 0, 0, 0, 0},
                            {2000, 13, 1, 0, 0, 0, 0}, {2000, 1, 0, 0, 0, 0, 0},
                            {1999, 1, 1, 0, 0, 0, 0}, {2100, 1, 1, 0, 0, 0, 0},
                            {2026, 9, 9, 0, 24, 0, 0}, {2026, 9, 9, 0, 12, 60, 0}}) {
        clock.value = date;
        assert(!CalendarForSleep(clock).valid);
    }
    clock = {{2026, 9, 9, 0, 12, 0, 0}, time::ClockSource::Uptime};
    assert(!CalendarForSleep(clock).valid);
    assert(std::strcmp(QuoteForSleep(CalendarForSleep(clock)).first, "A little reading") == 0);

    constexpr sdk::InputEvent ok{sdk::Button::Ok, sdk::InputAction::Click};
    constexpr sdk::InputEvent down{sdk::Button::Down, sdk::InputAction::Click};
    constexpr sdk::InputEvent up{sdk::Button::Up, sdk::InputAction::Click};
    constexpr sdk::InputEvent back{sdk::Button::Ok, sdk::InputAction::LongPress};
    for (const auto style : {SleepCoverStyle::Dashboard, SleepCoverStyle::Quote, SleepCoverStyle::Blank}) {
        SleepCoverController controller;
        assert(sdk::IsOk(controller.Start(style)) && controller.selected() == style);
        assert(controller.Tick() == SleepCoverDecision::None);
        assert(controller.Handle(back) == SleepCoverDecision::Back);
        assert(controller.Handle(up) == SleepCoverDecision::RenderFast);
        assert(controller.Handle(down) == SleepCoverDecision::RenderFast && controller.selected() == style);
        assert(controller.Handle(ok) == SleepCoverDecision::Choose && controller.scene() == SleepCoverScene::Preview);
        controller.Presented(false);
        assert(controller.Tick() == SleepCoverDecision::RenderQuality);
        controller.Presented(true);
        assert(controller.Tick() == SleepCoverDecision::None);
        assert(controller.Handle(up) == SleepCoverDecision::None && controller.selected() == style);
        assert(controller.Handle(ok) == SleepCoverDecision::Shutdown);
        assert(controller.Handle(back) == SleepCoverDecision::RenderQuality && controller.scene() == SleepCoverScene::Choose);
        assert(controller.selected() == style);
        assert(controller.Handle({sdk::Button::Down, sdk::InputAction::LongPress}) == SleepCoverDecision::Shutdown);
        controller.Handle(ok);
        controller.Stop();
        controller.Stop();
        controller.Presented(false);
        assert(controller.Tick() == SleepCoverDecision::None);
        assert(controller.Handle(ok) == SleepCoverDecision::None);
        assert(sdk::IsOk(controller.Start(style)) && controller.scene() == SleepCoverScene::Choose);
        assert(controller.selected() == style && controller.Tick() == SleepCoverDecision::None);
    }
    std::puts("PASS: sleep calendar, stable daily quotes, cover choices and deferred preview navigation.");
}
