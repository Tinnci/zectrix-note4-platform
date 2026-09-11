#include "zectrix_utilities.h"

#include <cassert>
#include <climits>
#include <cstdio>
#include <ctime>
#include <limits>

using namespace zectrix::app;
using namespace zectrix::sdk;
using namespace zectrix::time;

namespace {
constexpr InputEvent kUp{Button::Up, InputAction::Click}, kDown{Button::Down, InputAction::Click},
    kOk{Button::Ok, InputAction::Click}, kBack{Button::Ok, InputAction::LongPress},
    kOff{Button::Down, InputAction::LongPress}, kHoldUp{Button::Up, InputAction::LongPress};
constexpr int64_t kMinute = FocusTimer::kMinuteUs;
constexpr ClockSnapshot kClock{{2026, 9, 11, 0, 12, 0, 0}, ClockSource::Rtc};

void TestTimer() {
    FocusTimer timer;
    assert(timer.state() == FocusTimer::State::Ready && timer.MinutesRemaining(0) == 25);
    for (int i = 0; i < 30; ++i) timer.Adjust(-1);
    assert(timer.focus_minutes() == 5 && !timer.Adjust(-1));
    for (int i = 0; i < 30; ++i) timer.Adjust(1);
    assert(timer.focus_minutes() == 120 && !timer.Adjust(1));
    for (int i = 0; i < 19; ++i) timer.Adjust(-1);
    assert(timer.focus_minutes() == 25);
    timer.Confirm(10);
    assert(timer.state() == FocusTimer::State::Running && !timer.Adjust(-1));
    assert(timer.MinutesRemaining(10 + kMinute - 1) == 25);
    assert(timer.MinutesRemaining(10 + kMinute) == 24);
    timer.Confirm(10 + kMinute + 123);
    assert(timer.state() == FocusTimer::State::Paused);
    assert(timer.RemainingMicroseconds(1000 * kMinute) == 24 * kMinute - 123);
    timer.Confirm(1000 * kMinute);
    timer.Update(1024 * kMinute - 124);
    assert(timer.state() == FocusTimer::State::Running && timer.MinutesRemaining(1024 * kMinute - 124) == 1);
    timer.Update(1024 * kMinute - 123);
    assert(timer.state() == FocusTimer::State::Finished && timer.MinutesRemaining(1024 * kMinute) == 0);
    timer.Update(10000 * kMinute);
    assert(timer.phase() == FocusTimer::Phase::Focus && timer.state() == FocusTimer::State::Finished);
    timer.Confirm(10000 * kMinute);
    assert(timer.phase() == FocusTimer::Phase::Break && timer.MinutesRemaining(10000 * kMinute) == 5);
    // An OK at the deadline must complete, not silently start another interval.
    timer.Confirm(10005 * kMinute);
    assert(timer.state() == FocusTimer::State::Finished && timer.phase() == FocusTimer::Phase::Break);
    timer.Confirm(10006 * kMinute);
    assert(timer.phase() == FocusTimer::Phase::Focus && timer.MinutesRemaining(10006 * kMinute) == 25);
    timer.Reset();
    assert(timer.state() == FocusTimer::State::Ready && timer.focus_minutes() == 25);

    const auto maximum = std::numeric_limits<int64_t>::max();
    timer.Confirm(maximum - kMinute);
    assert(timer.MinutesRemaining(maximum) == 24);
    timer.Reset();
    timer.Confirm(-1);
    assert(timer.MinutesRemaining(std::numeric_limits<int64_t>::min()) == 25);
    timer.Update(maximum);
    assert(timer.state() == FocusTimer::State::Finished);
}

void TestCalendar() {
    UtilityMonth month;
    month.Set(1900, 2);
    assert(month.days() == 28 && month.first_weekday() == 3);
    month.Set(2000, 2);
    assert(month.days() == 29 && month.first_weekday() == 1);
    month.Set(2100, 2);
    assert(month.days() == 28 && month.first_weekday() == 0);
    month.Set(2026, 3);
    assert(month.first_weekday() == 6 && (month.first_weekday() + month.days() + 6) / 7 == 6);
    // Compare every month with the host's UTC calendar, including century edges.
    for (int year = UtilityMonth::kFirstYear; year <= UtilityMonth::kLastYear; ++year) {
        for (int number = 1; number <= 12; ++number) {
            month.Set(year, number);
            std::tm reference{};
            reference.tm_year = year - 1900;
            reference.tm_mon = number - 1;
            reference.tm_mday = 1;
            const auto first = timegm(&reference);
            assert(month.first_weekday() == (reference.tm_wday + 6) % 7);
            reference.tm_mon = number;
            const auto next = timegm(&reference);
            assert(next - first == month.days() * 86400);
        }
    }
    month.Set(1900, 1);
    assert(!month.Move(-1) && !month.Move(INT_MIN) && month.month() == 1);
    assert(!month.Set(1899, 12) && !month.Set(2000, 13));
    assert(month.Move(12) && month.year() == 1901 && month.month() == 1);
    month.Set(2199, 12);
    assert(!month.Move(1) && !month.Move(INT_MAX) && month.month() == 12);
    assert(month.Move(-12) && month.year() == 2198);
}

void TestTimerScenesAndLifetime() {
    UtilitySession session;
    {
        UtilityController controller(session);
        assert(controller.Start(0, kClock) == Status::Ok);
        assert(controller.Start(0, kClock) == Status::InvalidState);
        assert(controller.Tick(0, kClock) == UtilityDecision::RenderQuality);
        controller.Presented(true);
        assert(controller.Handle(kOk, 0, kClock) == UtilityDecision::RenderQuality);
        assert(controller.page() == UtilityPage::Focus);
        assert(controller.Handle(kDown, 0, kClock) == UtilityDecision::RenderFast);
        assert(controller.timer_minutes() == 20);
        assert(controller.Handle(kUp, 0, kClock) == UtilityDecision::RenderFast);
        assert(controller.Handle(kOk, 0, kClock) == UtilityDecision::RenderFast);
        assert(controller.Handle(kUp, 1, kClock) == UtilityDecision::None);
        assert(controller.Handle(kDown, 2, kClock) == UtilityDecision::None);
        unsigned draws = 0;
        auto changed_clock = kClock;
        for (int64_t second = 1; second <= 65; ++second) {
            changed_clock.value.year = second % 2 ? 2000 : 2099;
            changed_clock.value.minute = static_cast<int>(second % 60);
            if (controller.Tick(second * 1000000, changed_clock) != UtilityDecision::None) ++draws;
        }
        assert(draws == 1 && controller.timer_minutes() == 24);
        controller.Presented(false);
        assert(controller.Tick(65 * 1000000, kClock) == UtilityDecision::RenderQuality);
        controller.Presented(true);
        assert(controller.Tick(65 * 1000000, kClock) == UtilityDecision::None);
        assert(controller.Handle(kOk, 65 * 1000000, kClock) == UtilityDecision::RenderFast);
        assert(session.timer.state() == FocusTimer::State::Paused);
        assert(controller.Tick(500 * kMinute, kClock) == UtilityDecision::None);
        assert(controller.Handle(kUp, 500 * kMinute, kClock) == UtilityDecision::RenderFast);
        assert(session.timer.state() == FocusTimer::State::Ready);
        controller.Handle(kOk, 500 * kMinute, kClock);
        assert(controller.Handle(kBack, 501 * kMinute, kClock) == UtilityDecision::RenderQuality);
        assert(controller.page() == UtilityPage::Menu);
        assert(controller.Handle(kBack, 501 * kMinute, kClock) == UtilityDecision::Back);
        controller.Stop();
        controller.Stop();
        assert(controller.Handle(kOk, 1000 * kMinute, kClock) == UtilityDecision::None);
    }
    UtilityController reopened(session);
    assert(reopened.Start(510 * kMinute, kClock) == Status::Ok);
    assert(reopened.timer_minutes() == 15 && session.timer.state() == FocusTimer::State::Running);
    reopened.Tick(510 * kMinute, kClock);
    reopened.Handle(kOk, 510 * kMinute, kClock);
    assert(reopened.Handle(kOk, 525 * kMinute, kClock) == UtilityDecision::RenderFast);
    assert(session.timer.state() == FocusTimer::State::Finished);
    reopened.Handle(kOk, 526 * kMinute, kClock);
    assert(session.timer.phase() == FocusTimer::Phase::Break && reopened.timer_minutes() == 5);
    reopened.Handle(kBack, 526 * kMinute, kClock);
    reopened.Handle(kDown, 526 * kMinute, kClock);
    reopened.Handle(kOk, 526 * kMinute, kClock);
    assert(reopened.page() == UtilityPage::Calendar);
    // A hidden timer does not invalidate an unrelated utility page.
    assert(reopened.Tick(1000 * kMinute, kClock) == UtilityDecision::None);
    assert(session.timer.state() == FocusTimer::State::Finished);
    UtilitySession next_boot;
    assert(next_boot.timer.state() == FocusTimer::State::Ready && next_boot.count == 0);
}

void TestCalendarScenes() {
    UtilitySession session;
    UtilityController controller(session);
    controller.Start(0, {});
    controller.Tick(0, {});
    controller.Handle(kDown, 0, {});
    controller.Handle(kOk, 0, {});
    assert(controller.page() == UtilityPage::Calendar && !controller.has_today());
    assert(session.month.year() == 2000 && session.month.month() == 1);
    controller.Handle(kOk, 0, {});
    assert(controller.page() == UtilityPage::CalendarOptions && controller.selected() == 1);
    controller.Handle(kUp, 0, {});
    assert(controller.Handle(kOk, 0, {}) == UtilityDecision::None);
    assert(controller.page() == UtilityPage::CalendarOptions);
    controller.Handle(kDown, 0, {});
    controller.Handle(kOk, 0, {});
    assert(controller.page() == UtilityPage::CalendarJump);
    controller.Handle(kDown, 0, {});
    assert(controller.draft().year() == 1999 && session.month.year() == 2000);
    controller.Handle(kBack, 0, {});
    assert(controller.page() == UtilityPage::Calendar && session.month.year() == 2000);
    assert(controller.Tick(1, kClock) == UtilityDecision::RenderFast && controller.has_today());
    assert(session.month.year() == 2000);
    controller.Handle(kOk, 1, kClock);
    controller.Handle(kOk, 1, kClock);
    assert(controller.page() == UtilityPage::Calendar && session.month.year() == 2026 && session.month.month() == 9);
    controller.Handle(kOk, 1, kClock);
    controller.Handle(kDown, 1, kClock);
    controller.Handle(kOk, 1, kClock);
    controller.Handle(kUp, 1, kClock);
    controller.Handle(kOk, 1, kClock);
    controller.Handle(kDown, 1, kClock);
    controller.Handle(kOk, 1, kClock);
    assert(controller.page() == UtilityPage::Calendar && session.month.year() == 2027 && session.month.month() == 8);
    auto clock = kClock;
    ++clock.value.minute;
    assert(controller.Tick(2, clock) == UtilityDecision::None);
    ++clock.value.day;
    assert(controller.Tick(3, clock) == UtilityDecision::RenderFast && controller.today().day == 12);
    clock.value.month = 13;
    assert(controller.Tick(4, clock) == UtilityDecision::RenderFast && !controller.has_today());
}

void TestCounterAndGlobalNavigation() {
    UtilitySession session;
    UtilityController controller(session);
    controller.Start(0, kClock);
    controller.Tick(0, kClock);
    controller.Handle(kUp, 0, kClock);
    controller.Handle(kOk, 0, kClock);
    assert(controller.page() == UtilityPage::Counter);
    assert(controller.Handle(kDown, 0, kClock) == UtilityDecision::None && session.count == 0);
    assert(controller.Handle(kOk, 0, kClock) == UtilityDecision::None);
    for (unsigned i = 0; i < UtilitySession::kMaximumCount; ++i) controller.Handle(kUp, 0, kClock);
    assert(session.count == 9999 && controller.Handle(kUp, 0, kClock) == UtilityDecision::None);
    controller.Handle(kOk, 0, kClock);
    assert(session.count == 0 && session.undo_count == 9999);
    controller.Handle(kDown, 0, kClock);
    controller.Handle(kOk, 0, kClock);
    assert(session.count == 9999 && session.undo_count == 0);
    controller.Handle(kOk, 0, kClock);
    controller.Handle(kUp, 0, kClock);
    controller.Handle(kOk, 0, kClock);
    assert(session.undo_count == 1);
    controller.Handle(kOk, 0, kClock);
    controller.Handle(kBack, 0, kClock);
    assert(controller.selected() == 2);
    controller.Stop();
    UtilityController reopened(session);
    reopened.Start(0, kClock);
    assert(reopened.selected() == 2 && session.count == 1);

    for (const auto page : {UtilityPage::Menu, UtilityPage::Focus, UtilityPage::Calendar,
             UtilityPage::CalendarOptions, UtilityPage::CalendarJump, UtilityPage::Counter}) {
        UtilitySession test_session;
        UtilityController test(test_session);
        test.Start(0, kClock);
        test.Tick(0, kClock);
        if (page != UtilityPage::Menu) {
            if (page == UtilityPage::Counter) test.Handle(kUp, 0, kClock);
            else if (page != UtilityPage::Focus) test.Handle(kDown, 0, kClock);
            test.Handle(kOk, 0, kClock);
            if (page == UtilityPage::CalendarOptions || page == UtilityPage::CalendarJump) test.Handle(kOk, 0, kClock);
            if (page == UtilityPage::CalendarJump) {
                test.Handle(kDown, 0, kClock);
                test.Handle(kOk, 0, kClock);
            }
        }
        assert(test.page() == page);
        assert(test.Handle(kHoldUp, 0, kClock) == UtilityDecision::None);
        assert(test.Handle(kOff, 0, kClock) == UtilityDecision::Shutdown && test.page() == page);
        test.Stop();
        assert(test.Tick(0, kClock) == UtilityDecision::None);
    }
}
}

int main() {
    TestTimer();
    TestCalendar();
    TestTimerScenesAndLifetime();
    TestCalendarScenes();
    TestCounterAndGlobalNavigation();
    std::printf("PASS: pocket tools, minute-only timers, calendar, counter and scene lifetimes. Session RAM=%zu bytes.\n",
                sizeof(UtilitySession));
}
