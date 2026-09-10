#pragma once

#include <cstddef>
#include <cstdint>

#include "zectrix_scene_manager.h"
#include "zectrix_locale.h"

namespace zectrix::app {

enum class ConnectivityPage : SceneId { Actions, Forget };
enum class ConnectivityDecision : uint8_t {
    None,
    RenderFast,
    RenderQuality,
    StartPairing,
    FetchResource,
    ClearBonds,
    Back,
    Shutdown,
};

class ConnectivityController {
public:
    sdk::Status Start();
    void Stop();
    ConnectivityDecision Handle(const sdk::InputEvent& event);
    ConnectivityDecision Tick();
    void Presented(bool success) { if (!success) dirty_ = quality_ = true; }
    ConnectivityPage page() const { return static_cast<ConnectivityPage>(scenes_.current()); }
    std::size_t selected() const { return scenes_.state(scenes_.current()); }

private:
    static void Enter(void* context, SceneId scene);
    static bool Event(void* context, const SceneEvent& event);
    inline static constexpr SceneHandler kHandlers[] = {{Enter, Event, nullptr}, {Enter, Event, nullptr}};
    SceneManager scenes_{kHandlers, std::size(kHandlers), this};
    ConnectivityDecision action_ = ConnectivityDecision::None;
    bool dirty_ = false, quality_ = false;
};

struct ClockMinute {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
};

bool ClockDisplayChanged(const ClockMinute& displayed,
                         const ClockMinute& current);

enum class SettingsPage : SceneId { Options, Language };
enum class SettingsDecision : uint8_t {
    None,
    RenderFast,
    RenderQuality,
    Save,
    SaveLanguage,
    Back,
    Shutdown,
};

struct SettingsResult {
    SettingsDecision decision = SettingsDecision::None;
    bool auto_showcase = false;
    i18n::Language language = i18n::Language::English;
};

class SettingsController {
public:
    explicit SettingsController(bool auto_showcase, i18n::Language language = i18n::CurrentLanguage())
        : auto_showcase_(auto_showcase), language_(language) {}

    sdk::Status Start();
    sdk::Status Start(bool auto_showcase, i18n::Language language);
    void Stop();
    SettingsResult Handle(const sdk::InputEvent& event);
    SettingsResult Tick();
    void Presented(bool success) { if (!success) dirty_ = quality_ = true; }
    void SaveCompleted(bool success) { save_failed_ = !success; }
    bool auto_showcase() const { return auto_showcase_; }
    i18n::Language language() const { return language_; }
    SettingsPage page() const { return static_cast<SettingsPage>(scenes_.current()); }
    std::size_t selected() const { return scenes_.state(scenes_.current()); }
    std::size_t option_count() const { return i18n::LanguageCount() > 1 ? 2 : 1; }

private:
    static void Enter(void* context, SceneId scene);
    static bool Event(void* context, const SceneEvent& event);
    inline static constexpr SceneHandler kHandlers[] = {{Enter, Event, nullptr}, {Enter, Event, nullptr}};
    SceneManager scenes_{kHandlers, std::size(kHandlers), this};
    bool auto_showcase_;
    i18n::Language language_;
    SettingsDecision action_ = SettingsDecision::None;
    bool dirty_ = false, quality_ = false;
    bool save_failed_ = false;
};

constexpr uint32_t kAutoShowcaseDefault = 0;
constexpr char kAutoShowcaseSettingKey[] = "ui.auto_demo";

bool NormalizeAutoShowcaseSetting(uint32_t stored, bool* value);

enum class DiagnosticsPage : SceneId { Mode, Individual, Running, Summary };
enum class DiagnosticsDecision : uint8_t {
    None,
    RenderFast,
    RenderQuality,
    RunAll,
    RunSelected,
    Back,
    Shutdown,
};

struct DiagnosticsResult {
    DiagnosticsDecision decision = DiagnosticsDecision::None;
    DiagnosticsPage page = DiagnosticsPage::Mode;
    std::size_t selected = 0;
};

class DiagnosticsController {
public:
    static constexpr std::size_t kTestCount = 7;

    sdk::Status Start();
    void Stop();
    DiagnosticsResult Handle(const sdk::InputEvent& event);
    DiagnosticsResult FinishRun(bool cancelled);
    DiagnosticsResult Tick();
    void Presented(bool success) { if (!success) dirty_ = quality_ = true; }
    DiagnosticsPage page() const { return static_cast<DiagnosticsPage>(scenes_.current()); }
    std::size_t selected() const;

private:
    static void Enter(void* context, SceneId scene);
    static bool Event(void* context, const SceneEvent& event);
    inline static constexpr SceneHandler kHandlers[] = {
        {Enter, Event, nullptr}, {Enter, Event, nullptr},
        {Enter, Event, nullptr}, {Enter, Event, nullptr},
    };
    SceneManager scenes_{kHandlers, std::size(kHandlers), this};
    DiagnosticsDecision action_ = DiagnosticsDecision::None;
    bool run_all_ = false, finish_requested_ = false, cancelled_ = false;
    bool dirty_ = false, quality_ = false;
};

}  // namespace zectrix::app
