#pragma once

#include <cstddef>
#include <cstdint>

#include "zectrix_input_service.h"

namespace zectrix::app {

enum class LauncherDecision : uint8_t {
    None,
    RenderFast,
    OpenClock,
    OpenSettings,
    OpenDiagnostics,
    RunLegacy,
    Shutdown,
};

struct LauncherResult {
    LauncherDecision decision = LauncherDecision::None;
    std::size_t selected = 0;
};

class LauncherController {
public:
    static constexpr std::size_t kItemCount = 7;

    LauncherResult Handle(const input::InputEvent& event);
    std::size_t selected() const { return selected_; }

private:
    std::size_t selected_ = 0;
};

enum class ClockDecision : uint8_t { None, Home, Shutdown };

ClockDecision HandleClockInput(const input::InputEvent& event);

enum class SettingsDecision : uint8_t {
    None,
    RenderFast,
    Save,
    Home,
    Shutdown,
};

struct SettingsResult {
    SettingsDecision decision = SettingsDecision::None;
    bool auto_showcase = true;
};

class SettingsController {
public:
    explicit SettingsController(bool auto_showcase)
        : auto_showcase_(auto_showcase) {}

    SettingsResult Handle(const input::InputEvent& event);
    bool auto_showcase() const { return auto_showcase_; }

private:
    bool auto_showcase_;
};

constexpr uint32_t kAutoShowcaseDefault = 1;
constexpr char kAutoShowcaseSettingKey[] = "ui.auto_demo";

bool NormalizeAutoShowcaseSetting(uint32_t stored, bool* value);

enum class DiagnosticsPage : uint8_t { Mode, Individual, Summary };
enum class DiagnosticsDecision : uint8_t {
    None,
    RenderFast,
    RunAll,
    RunSelected,
    Home,
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

    DiagnosticsResult Handle(const input::InputEvent& event);
    void ShowSummary() { page_ = DiagnosticsPage::Summary; }
    DiagnosticsPage page() const { return page_; }
    std::size_t selected() const { return selected_; }

private:
    DiagnosticsPage page_ = DiagnosticsPage::Mode;
    std::size_t selected_ = 0;
};

}  // namespace zectrix::app
