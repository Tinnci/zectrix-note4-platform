#include "zectrix_sleep_cover.h"

namespace zectrix::app {

SleepCoverStyle SleepCoverSetting(uint32_t value) {
    return value <= static_cast<uint32_t>(SleepCoverStyle::Picture)
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
        {"A little reading", "goes a long way.", i18n::Text::QuoteReadingFirst, i18n::Text::QuoteReadingSecond},
        {"Keep a page for tomorrow.", "Let today settle.", i18n::Text::QuoteTomorrowFirst, i18n::Text::QuoteTomorrowSecond},
        {"Take the slow path.", "Notice what stays.", i18n::Text::QuoteSlowFirst, i18n::Text::QuoteSlowSecond},
        {"Leave room in your day", "for a good sentence.", i18n::Text::QuoteSentenceFirst, i18n::Text::QuoteSentenceSecond},
        {"Small pages.", "Wide horizons.", i18n::Text::QuotePagesFirst, i18n::Text::QuotePagesSecond},
        {"One quiet chapter", "is time well spent.", i18n::Text::QuoteChapterFirst, i18n::Text::QuoteChapterSecond},
        {"Pause here.", "The next page can wait.", i18n::Text::QuotePauseFirst, i18n::Text::QuotePauseSecond},
    };
    return quotes[calendar.valid ? calendar.day_number % std::size(quotes) : 0];
}

sdk::Status SleepCoverController::Start(SleepCoverStyle selected) {
    if (scenes_.depth()) return sdk::Status::InvalidState;
    const auto result = scenes_.Start(0);
    const auto normalized = static_cast<uint32_t>(SleepCoverSetting(static_cast<uint32_t>(selected)));
    scenes_.SetState(0, normalized < kSleepCoverStyleCount ? normalized : 0);
    action_ = SleepCoverDecision::None;
    dirty_ = quality_ = false;
    return result;
}

void SleepCoverController::Stop() {
    scenes_.Stop();
    action_ = SleepCoverDecision::None;
    dirty_ = quality_ = false;
}

void SleepCoverController::Enter(void* context, SceneId) {
    auto& self = *static_cast<SleepCoverController*>(context);
    self.dirty_ = self.quality_ = true;
}

bool SleepCoverController::Event(void* context, const SceneEvent& event) {
    auto& self = *static_cast<SleepCoverController*>(context);
    if (event.type != SceneEvent::Type::Input) return false;
    const auto key = MapNavigation(event.input);
    if (key == Navigation::Confirm) {
        if (self.scene() == SleepCoverScene::Choose) {
            self.action_ = SleepCoverDecision::Choose;
            self.scenes_.Push(1);
        } else self.action_ = SleepCoverDecision::Shutdown;
    } else if (self.scene() == SleepCoverScene::Choose &&
               (key == Navigation::Previous || key == Navigation::Next)) {
        const auto selected = self.scenes_.state(0);
        self.scenes_.SetState(0, MoveSelection(selected, kSleepCoverStyleCount, key));
        self.dirty_ = true;
    } else return false;
    return true;
}

SleepCoverDecision SleepCoverController::Handle(const sdk::InputEvent& input) {
    if (!scenes_.depth()) return SleepCoverDecision::None;
    const auto key = MapNavigation(input);
    if (key == Navigation::Shutdown) return SleepCoverDecision::Shutdown;
    const bool back = key == Navigation::Back;
    if (!scenes_.Dispatch({back ? SceneEvent::Type::Back : SceneEvent::Type::Input, input, 0}) && back)
        return SleepCoverDecision::Back;
    return Tick();
}

SleepCoverDecision SleepCoverController::Tick() {
    if (!scenes_.depth()) return SleepCoverDecision::None;
    const auto result = action_ != SleepCoverDecision::None ? action_ : !dirty_ ? SleepCoverDecision::None :
        quality_ ? SleepCoverDecision::RenderQuality : SleepCoverDecision::RenderFast;
    action_ = SleepCoverDecision::None;
    dirty_ = quality_ = false;
    return result;
}

}  // namespace zectrix::app
