#include "zectrix_utilities.h"

#include <algorithm>

namespace zectrix::app {
namespace {
constexpr SceneId Id(UtilityPage page) { return static_cast<SceneId>(page); }
}

bool FocusTimer::Adjust(int direction) {
    if (state_ != State::Ready || direction == 0) return false;
    const auto minutes = static_cast<unsigned>(std::clamp(
        static_cast<int>(focus_minutes_) + (direction > 0 ? 5 : -5), 5, 120));
    if (minutes == focus_minutes_) return false;
    focus_minutes_ = minutes;
    return true;
}

int64_t FocusTimer::RemainingMicroseconds(int64_t now_us) const {
    if (state_ == State::Ready) return focus_minutes_ * kMinuteUs;
    if (state_ != State::Running) return remaining_us_;
    // Subtract nonnegative monotonic samples rather than adding a deadline
    // that could overflow. A stale sample cannot increase the saved duration.
    const int64_t elapsed = now_us > anchor_us_ ? now_us - anchor_us_ : 0;
    return elapsed >= remaining_us_ ? 0 : remaining_us_ - elapsed;
}

unsigned FocusTimer::MinutesRemaining(int64_t now_us) const {
    const auto remaining = RemainingMicroseconds(now_us);
    return static_cast<unsigned>((remaining + kMinuteUs - 1) / kMinuteUs);
}

void FocusTimer::Update(int64_t now_us) {
    if (state_ == State::Running && RemainingMicroseconds(now_us) == 0) {
        remaining_us_ = 0;
        state_ = State::Finished;
    }
}

void FocusTimer::Confirm(int64_t now_us) {
    if (state_ == State::Running) {
        remaining_us_ = RemainingMicroseconds(now_us);
        state_ = remaining_us_ == 0 ? State::Finished : State::Paused;
        return;
    }
    if (state_ == State::Ready) remaining_us_ = focus_minutes_ * kMinuteUs;
    else if (state_ == State::Finished) {
        phase_ = phase_ == Phase::Focus ? Phase::Break : Phase::Focus;
        remaining_us_ = (phase_ == Phase::Focus ? focus_minutes_ : kBreakMinutes) * kMinuteUs;
    }
    // A late callback completes one interval; the next always needs an OK.
    anchor_us_ = std::max<int64_t>(0, now_us);
    state_ = State::Running;
}

void FocusTimer::Reset() {
    state_ = State::Ready;
    phase_ = Phase::Focus;
    remaining_us_ = 0;
}

bool UtilityMonth::Set(int year, int month) {
    if (year < kFirstYear || year > kLastYear || month < 1 || month > 12 ||
        (year == year_ && month == month_)) return false;
    year_ = year;
    month_ = month;
    return true;
}

bool UtilityMonth::Move(int months) {
    const int64_t index = static_cast<int64_t>(year_) * 12 + month_ - 1 + months;
    if (index < kFirstYear * 12 || index >= (kLastYear + 1) * 12) return false;
    return Set(static_cast<int>(index / 12), static_cast<int>(index % 12) + 1);
}

int UtilityMonth::first_weekday() const {
    const int previous_year = year_ - 1;
    int days = previous_year * 365 + previous_year / 4 - previous_year / 100 + previous_year / 400;
    for (int month = 1; month < month_; ++month) days += time::DaysInMonth(year_, month);
    // Gregorian day zero is Monday; the RTC weekday field is not authoritative.
    return days % 7;
}

sdk::Status UtilityController::Start(int64_t now_us, const time::ClockSnapshot& clock) {
    if (scenes_.depth()) return sdk::Status::InvalidState;
    ReadClock(clock);
    if (!session_.calendar_initialized) {
        if (has_today_) session_.month.Set(today_.year, today_.month);
        session_.calendar_initialized = true;
    }
    session_.timer.Update(now_us);
    timer_state_ = session_.timer.state();
    timer_minutes_ = session_.timer.MinutesRemaining(now_us);
    const auto result = scenes_.Start(Id(UtilityPage::Menu));
    scenes_.SetState(Id(UtilityPage::Menu), session_.selected < 3 ? session_.selected : 0);
    return result;
}

void UtilityController::Stop() {
    scenes_.Stop();
    dirty_ = quality_ = false;
}

void UtilityController::Enter(void* context, SceneId scene) {
    auto& self = *static_cast<UtilityController*>(context);
    if (scene == Id(UtilityPage::CalendarOptions) || scene == Id(UtilityPage::CalendarJump))
        self.scenes_.SetState(scene, 0);
    if (scene == Id(UtilityPage::CalendarOptions) && !self.has_today_) self.scenes_.SetState(scene, 1);
    if (scene == Id(UtilityPage::CalendarJump)) self.draft_ = self.session_.month;
    self.dirty_ = self.quality_ = true;
}

bool UtilityController::Event(void* context, const SceneEvent& event) {
    auto& self = *static_cast<UtilityController*>(context);
    if (event.type != SceneEvent::Type::Input) return false;
    const auto key = MapNavigation(event.input);
    const bool up = key == Navigation::Previous, down = key == Navigation::Next;
    const bool ok = key == Navigation::Confirm;
    if (!up && !down && !ok) return false;
    auto& session = self.session_;
    switch (self.page()) {
        case UtilityPage::Menu:
            if (ok) {
                constexpr UtilityPage pages[] = {UtilityPage::Focus, UtilityPage::Calendar, UtilityPage::Counter};
                self.scenes_.Push(Id(pages[self.selected()]));
            } else {
                session.selected = MoveSelection(self.selected(), 3, key);
                self.scenes_.SetState(Id(UtilityPage::Menu), session.selected);
                self.dirty_ = true;
            }
            break;
        case UtilityPage::Focus:
            if (ok) {
                session.timer.Confirm(event.now_us);
                self.dirty_ = true;
            } else if (session.timer.state() == FocusTimer::State::Ready) {
                self.dirty_ |= session.timer.Adjust(up ? 1 : -1);
            } else if (up && session.timer.state() != FocusTimer::State::Running) {
                session.timer.Reset();
                self.dirty_ = true;
            }
            break;
        case UtilityPage::Calendar:
            if (ok) self.scenes_.Push(Id(UtilityPage::CalendarOptions));
            else self.dirty_ |= session.month.Move(up ? -1 : 1);
            break;
        case UtilityPage::CalendarOptions:
            if (!ok) {
                self.scenes_.SetState(Id(UtilityPage::CalendarOptions), MoveSelection(self.selected(), 2, key));
                self.dirty_ = true;
            } else if (self.selected() == 1) {
                self.scenes_.Replace(Id(UtilityPage::CalendarJump));
            } else if (self.has_today_) {
                session.month.Set(self.today_.year, self.today_.month);
                self.scenes_.Pop();
            }
            break;
        case UtilityPage::CalendarJump:
            if (ok) {
                if (self.selected() == 0) {
                    self.scenes_.SetState(Id(UtilityPage::CalendarJump), 1);
                    self.dirty_ = true;
                } else {
                    session.month = self.draft_;
                    self.scenes_.Pop();
                }
            } else if (self.selected() == 0) {
                self.dirty_ |= self.draft_.Set(self.draft_.year() + (up ? 1 : -1), self.draft_.month());
            } else {
                const int month = (self.draft_.month() - 1 + (up ? 1 : 11)) % 12 + 1;
                self.dirty_ |= self.draft_.Set(self.draft_.year(), month);
            }
            break;
        case UtilityPage::Counter: {
            const unsigned previous = session.count;
            if (ok) {
                if (session.count) { session.undo_count = session.count; session.count = 0; }
                else { session.count = session.undo_count; session.undo_count = 0; }
            } else if (up && session.count < UtilitySession::kMaximumCount) {
                ++session.count;
                session.undo_count = 0;
            } else if (down && session.count) {
                --session.count;
                session.undo_count = 0;
            }
            self.dirty_ |= previous != session.count;
            break;
        }
    }
    return true;
}

void UtilityController::ReadClock(const time::ClockSnapshot& clock) {
    const bool valid = clock.source != time::ClockSource::Uptime && time::IsValid(clock.value);
    if (has_today_ != valid || (valid && (today_.year != clock.value.year ||
        today_.month != clock.value.month || today_.day != clock.value.day))) {
        has_today_ = valid;
        today_ = clock.value;
        if (page() == UtilityPage::Calendar || page() == UtilityPage::CalendarOptions) dirty_ = true;
    }
}

UtilityDecision UtilityController::Handle(const sdk::InputEvent& input, int64_t now_us,
                                          const time::ClockSnapshot& clock) {
    if (!scenes_.depth()) return UtilityDecision::None;
    const auto key = MapNavigation(input);
    if (key == Navigation::Shutdown) return UtilityDecision::Shutdown;
    ReadClock(clock);
    const bool back = key == Navigation::Back;
    if (!scenes_.Dispatch({back ? SceneEvent::Type::Back : SceneEvent::Type::Input, input, now_us}) && back)
        return UtilityDecision::Back;
    return Tick(now_us, clock);
}

UtilityDecision UtilityController::Tick(int64_t now_us, const time::ClockSnapshot& clock) {
    if (!scenes_.depth()) return UtilityDecision::None;
    ReadClock(clock);
    session_.timer.Update(now_us);
    const auto minutes = session_.timer.MinutesRemaining(now_us);
    if (timer_minutes_ != minutes || timer_state_ != session_.timer.state()) {
        timer_minutes_ = minutes;
        timer_state_ = session_.timer.state();
        if (page() == UtilityPage::Focus || page() == UtilityPage::Menu) dirty_ = true;
    }
    return RenderDecision();
}

UtilityDecision UtilityController::RenderDecision() {
    const auto decision = !dirty_ ? UtilityDecision::None : quality_ ?
        UtilityDecision::RenderQuality : UtilityDecision::RenderFast;
    dirty_ = quality_ = false;
    return decision;
}

}  // namespace zectrix::app
