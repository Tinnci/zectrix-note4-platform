#pragma once

#include <array>
#include "sdkconfig.h"

#include "zectrix_power_service.h"
#include "zectrix_scene_manager.h"
#include "zectrix_time_service.h"
#include "zectrix_locale.h"

namespace zectrix::app {

enum class SleepCoverStyle : uint8_t { Dashboard, Quote, Blank, Picture };
#if CONFIG_ZECTRIX_ENABLE_BOOK_STORAGE
constexpr unsigned kSleepCoverStyleCount = 4;
#else
constexpr unsigned kSleepCoverStyleCount = 3;
#endif
constexpr char kSleepCoverSettingKey[] = "ui.sleep_cover";
constexpr SleepCoverStyle kSleepCoverDefault = SleepCoverStyle::Dashboard;
SleepCoverStyle SleepCoverSetting(uint32_t value);

struct SleepCoverImage {
    void* context = nullptr;
    bool (*read)(void* context, uint32_t offset, void* output, std::size_t size) = nullptr;
};

struct SleepCoverSnapshot {
    time::ClockSnapshot clock{};
    power::PowerSnapshot power{};
    // The dashboard needs only a copied title and progress, not a reader engine.
    struct Reading {
        std::array<char, 64> book_id{};
        uint16_t progress_per_mille = 0;
    } reading;
    bool has_reading = false;
    std::array<char, 112> weather_line{};
};

struct SleepCalendar {
    bool valid = false;
    uint8_t days = 0, first_weekday = 0, weekday = 0;
    uint32_t day_number = 0;
};

struct SleepQuote {
    const char* first;
    const char* second;
    i18n::Text first_text = i18n::Text::None;
    i18n::Text second_text = i18n::Text::None;
};
SleepCalendar CalendarForSleep(const time::ClockSnapshot& clock);
const SleepQuote& QuoteForSleep(const SleepCalendar& calendar);

enum class SleepCoverScene : uint8_t { Choose, Preview };
enum class SleepCoverDecision : uint8_t { None, RenderFast, RenderQuality, Choose, Back, Shutdown };

class SleepCoverController {
public:
    void ObserveScenes(SceneSnapshot* snapshot) { scenes_.ObserveScenes(snapshot); }
    sdk::Status Start(SleepCoverStyle selected);
    void Stop();
    SleepCoverDecision Handle(const sdk::InputEvent& input);
    SleepCoverDecision Tick();
    void Presented(bool success) { if (!success) dirty_ = quality_ = true; }
    SleepCoverScene scene() const { return static_cast<SleepCoverScene>(scenes_.current()); }
    SleepCoverStyle selected() const { return static_cast<SleepCoverStyle>(scenes_.state(0)); }

private:
    static void Enter(void* context, SceneId);
    static bool Event(void* context, const SceneEvent& event);
    inline static constexpr SceneHandler kHandlers[] = {{Enter, Event, nullptr}, {Enter, Event, nullptr}};
    SceneManager scenes_{kHandlers, std::size(kHandlers), this};
    SleepCoverDecision action_ = SleepCoverDecision::None;
    bool dirty_ = false, quality_ = false;
};

}  // namespace zectrix::app
