#include "zectrix_sleep_cover.h"

namespace zectrix::app {

SleepCoverStyle SleepCoverSetting(uint32_t value) {
    return value <= static_cast<uint32_t>(SleepCoverStyle::Blank)
        ? static_cast<SleepCoverStyle>(value) : kSleepCoverDefault;
}

SleepCalendar CalendarForSleep(const time::ClockSnapshot& clock) {
    const auto& date = clock.value;
    if (clock.source == time::ClockSource::Uptime || date.year < 2000 || date.year > 2099 ||
        date.month < 1 || date.month > 12 || date.hour < 0 || date.hour > 23 || date.minute < 0 || date.minute > 59)
        return {};
    constexpr uint8_t lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = date.year % 4 == 0 && (date.year % 100 != 0 || date.year % 400 == 0);
    const unsigned days = lengths[date.month - 1] + (date.month == 2 && leap ? 1 : 0);
    if (date.day < 1 || static_cast<unsigned>(date.day) > days) return {};
    const uint32_t year = date.year - 1;
    uint32_t first = year * 365 + year / 4 - year / 100 + year / 400;
    for (int month = 1; month < date.month; ++month) first += lengths[month - 1] + (month == 2 && leap ? 1 : 0);
    const uint32_t day = first + date.day - 1;
    // Gregorian day zero is Monday. Ignore an inconsistent RTC weekday field.
    return {true, static_cast<uint8_t>(days), static_cast<uint8_t>(first % 7),
            static_cast<uint8_t>(day % 7), day};
}

const SleepQuote& QuoteForSleep(const SleepCalendar& calendar) {
    static constexpr SleepQuote quotes[] = {
        {"A little reading", "goes a long way."},
        {"Keep a page for tomorrow.", "Let today settle."},
        {"Take the slow path.", "Notice what stays."},
        {"Leave room in your day", "for a good sentence."},
        {"Small pages.", "Wide horizons."},
        {"One quiet chapter", "is time well spent."},
        {"Pause here.", "The next page can wait."},
    };
    return quotes[calendar.valid ? calendar.day_number % std::size(quotes) : 0];
}

sdk::Status SleepCoverController::Start(SleepCoverStyle selected) {
    const auto result = scenes_.Start(0);
    scenes_.SetState(0, static_cast<uint32_t>(SleepCoverSetting(static_cast<uint32_t>(selected))));
    action_ = SleepCoverDecision::None;
    dirty_ = quality_ = false;
    return result;
}

void SleepCoverController::Enter(void* context, SceneId) {
    auto& self = *static_cast<SleepCoverController*>(context);
    self.dirty_ = self.quality_ = true;
}

bool SleepCoverController::Event(void* context, const SceneEvent& event) {
    auto& self = *static_cast<SleepCoverController*>(context);
    if (event.type == SceneEvent::Type::Back) {
        if (self.scene() == SleepCoverScene::Choose) return false;
        self.scenes_.Pop();
        return true;
    }
    if (event.type != SceneEvent::Type::Input || event.input.action != sdk::InputAction::Click) return false;
    if (event.input.button == sdk::Button::Ok) {
        if (self.scene() == SleepCoverScene::Choose) {
            self.action_ = SleepCoverDecision::Choose;
            self.scenes_.Push(1);
        } else self.action_ = SleepCoverDecision::Shutdown;
    } else if (self.scene() == SleepCoverScene::Choose) {
        const auto selected = self.scenes_.state(0);
        self.scenes_.SetState(0, (selected + (event.input.button == sdk::Button::Up ? 2 : 1)) % 3);
        self.dirty_ = true;
    }
    return true;
}

SleepCoverDecision SleepCoverController::Handle(const sdk::InputEvent& input) {
    if (input.button == sdk::Button::Down && input.action == sdk::InputAction::LongPress)
        return SleepCoverDecision::Shutdown;
    const bool back = input.button == sdk::Button::Ok && input.action == sdk::InputAction::LongPress;
    if (!scenes_.Dispatch({back ? SceneEvent::Type::Back : SceneEvent::Type::Input, input, 0}) && back)
        return SleepCoverDecision::Home;
    return Tick();
}

SleepCoverDecision SleepCoverController::Tick() {
    const auto result = action_ != SleepCoverDecision::None ? action_ : !dirty_ ? SleepCoverDecision::None :
        quality_ ? SleepCoverDecision::RenderQuality : SleepCoverDecision::RenderFast;
    action_ = SleepCoverDecision::None;
    dirty_ = quality_ = false;
    return result;
}

}  // namespace zectrix::app
