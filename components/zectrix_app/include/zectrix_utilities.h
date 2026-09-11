#pragma once

#include "zectrix_scene_manager.h"
#include "zectrix_time_service.h"

namespace zectrix::app {

class FocusTimer {
public:
    enum class State : uint8_t { Ready, Running, Paused, Finished };
    enum class Phase : uint8_t { Focus, Break };
    static constexpr int64_t kMinuteUs = 60 * 1000000LL;
    static constexpr unsigned kBreakMinutes = 5;

    bool Adjust(int direction);
    void Confirm(int64_t now_us);
    void Reset();
    void Update(int64_t now_us);
    int64_t RemainingMicroseconds(int64_t now_us) const;
    unsigned MinutesRemaining(int64_t now_us) const;
    unsigned focus_minutes() const { return focus_minutes_; }
    State state() const { return state_; }
    Phase phase() const { return phase_; }

private:
    State state_ = State::Ready;
    Phase phase_ = Phase::Focus;
    unsigned focus_minutes_ = 25;
    int64_t anchor_us_ = 0;
    int64_t remaining_us_ = 0;
};

class UtilityMonth {
public:
    static constexpr int kFirstYear = 1900, kLastYear = 2199;
    bool Set(int year, int month);
    bool Move(int months);
    int year() const { return year_; }
    int month() const { return month_; }
    int days() const { return time::DaysInMonth(year_, month_); }
    int first_weekday() const;

private:
    int year_ = 2000, month_ = 1;
};

// The shell keeps this small session in RAM across foreground app lifetimes.
// It needs no background task, storage writes or resources to release.
struct UtilitySession {
    static constexpr unsigned kMaximumCount = 9999;
    FocusTimer timer;
    UtilityMonth month;
    unsigned count = 0, undo_count = 0;
    uint32_t selected = 0;
    bool calendar_initialized = false;
};

enum class UtilityPage : SceneId { Menu, Focus, Calendar, CalendarOptions, CalendarJump, Counter };
enum class UtilityDecision : uint8_t { None, RenderFast, RenderQuality, Back, Shutdown };

class UtilityController {
public:
    void ObserveScenes(SceneSnapshot* snapshot) { scenes_.ObserveScenes(snapshot); }
    explicit UtilityController(UtilitySession& session) : session_(session) {}
    sdk::Status Start(int64_t now_us, const time::ClockSnapshot& clock);
    void Stop();
    UtilityDecision Handle(const sdk::InputEvent& input, int64_t now_us, const time::ClockSnapshot& clock);
    UtilityDecision Tick(int64_t now_us, const time::ClockSnapshot& clock);
    void Presented(bool success) { if (!success) dirty_ = quality_ = true; }
    UtilityPage page() const { return static_cast<UtilityPage>(scenes_.current()); }
    std::size_t selected() const { return scenes_.state(scenes_.current()); }
    const UtilitySession& session() const { return session_; }
    const UtilityMonth& draft() const { return draft_; }
    const time::DateTime& today() const { return today_; }
    bool has_today() const { return has_today_; }
    unsigned timer_minutes() const { return timer_minutes_; }

private:
    static void Enter(void* context, SceneId scene);
    static bool Event(void* context, const SceneEvent& event);
    void ReadClock(const time::ClockSnapshot& clock);
    UtilityDecision RenderDecision();
    inline static constexpr SceneHandler kHandlers[] = {
        {Enter, Event, nullptr}, {Enter, Event, nullptr}, {Enter, Event, nullptr},
        {Enter, Event, nullptr}, {Enter, Event, nullptr}, {Enter, Event, nullptr},
    };
    UtilitySession& session_;
    SceneManager scenes_{kHandlers, std::size(kHandlers), this};
    UtilityMonth draft_;
    time::DateTime today_{};
    unsigned timer_minutes_ = 0;
    FocusTimer::State timer_state_ = FocusTimer::State::Ready;
    bool has_today_ = false, dirty_ = false, quality_ = false;
};

}  // namespace zectrix::app
