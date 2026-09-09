#include "zectrix_first_party_app_controllers.h"

#include <algorithm>
#include <array>
#include <cassert>

int main() {
    using namespace zectrix::app;
    using Action = zectrix::sdk::InputAction;
    using zectrix::sdk::Button;
    using zectrix::sdk::InputEvent;

    LauncherController launcher;
    LauncherResult result = launcher.Handle({Button::Ok, Action::Click});
#if CONFIG_ZECTRIX_ENABLE_READER
    assert(result.decision == LauncherDecision::OpenReader);
#elif CONFIG_ZECTRIX_ENABLE_BOOK_TRANSFER
    assert(result.decision == LauncherDecision::OpenBookTransfer);
#else
    assert(result.decision == LauncherDecision::OpenClock);
#endif
    assert(result.selected == 0);

    std::array<LauncherDecision, LauncherController::kItemCount> visited{};
    for (std::size_t selected = 0; selected < visited.size(); ++selected) {
        result = launcher.Handle({Button::Ok, Action::Click});
        assert(result.selected == selected);
        visited[selected] = result.decision;
        LauncherController restored(selected);
        assert(restored.Handle({Button::Ok, Action::Click}).decision == result.decision);
        result = launcher.Handle({Button::Down, Action::Click});
        assert(result.decision == LauncherDecision::RenderFast);
        assert(result.selected == (selected + 1) % visited.size());
    }
    const auto count = [&](LauncherDecision decision) {
        return std::count(visited.begin(), visited.end(), decision);
    };
    const LauncherDecision core_destinations[] = {
        LauncherDecision::OpenClock, LauncherDecision::OpenSleepCover, LauncherDecision::OpenSettings,
        LauncherDecision::OpenShowcase,
        LauncherDecision::OpenGallery, LauncherDecision::OpenDiagnostics,
        LauncherDecision::OpenDeviceInfo, LauncherDecision::OpenAbout};
    for (const auto destination : core_destinations) assert(count(destination) == 1);
    assert(count(LauncherDecision::OpenReader) == CONFIG_ZECTRIX_ENABLE_READER);
    assert(count(LauncherDecision::OpenBookTransfer) == CONFIG_ZECTRIX_ENABLE_BOOK_TRANSFER);
    assert(count(LauncherDecision::OpenConnectivity) == CONFIG_ZECTRIX_ENABLE_CONNECTIVITY);
    assert(visited.size() == std::size(core_destinations) + CONFIG_ZECTRIX_ENABLE_READER +
           CONFIG_ZECTRIX_ENABLE_BOOK_TRANSFER + CONFIG_ZECTRIX_ENABLE_CONNECTIVITY);
    result = launcher.Handle({Button::Up, Action::Click});
    assert(result.selected == LauncherController::kItemCount - 1);
    assert(LauncherController(100).selected() == 0);
    assert(kAutoShowcaseDefault == 0);
    result = launcher.Handle({Button::Down, Action::LongPress});
    assert(result.decision == LauncherDecision::Shutdown);
    assert(result.selected == LauncherController::kItemCount - 1);

    result = launcher.Handle({Button::Ok, Action::LongPress});
    assert(result.decision == LauncherDecision::None);
    assert(HandleClockInput({Button::Ok, Action::LongPress}) ==
           ClockDecision::Home);
    assert(HandleClockInput({Button::Down, Action::LongPress}) ==
           ClockDecision::Shutdown);
    assert(HandleClockInput({Button::Ok, Action::Click}) ==
           ClockDecision::None);
    assert(HandleClockInput({Button::Up, Action::LongPress}) ==
           ClockDecision::None);
    assert(HandleConnectivityInput({Button::Ok, Action::Click}) ==
           ConnectivityDecision::StartPairing);
    assert(HandleConnectivityInput({Button::Up, Action::LongPress}) ==
           ConnectivityDecision::ClearBonds);
    assert(HandleConnectivityInput({Button::Ok, Action::LongPress}) ==
           ConnectivityDecision::Home);
    assert(HandleConnectivityInput({Button::Down, Action::LongPress}) ==
           ConnectivityDecision::Shutdown);
    assert(HandleConnectivityInput({Button::Down, Action::Click}) ==
           ConnectivityDecision::None);
    assert(HandleConnectivityInput({Button::Up, Action::Click}) ==
           ConnectivityDecision::FetchResource);
    assert(!ClockDisplayChanged({2026, 8, 12, 10, 30},
                                {2026, 8, 12, 10, 30}));
    assert(ClockDisplayChanged({2026, 8, 12, 10, 30},
                               {2026, 8, 12, 10, 31}));
    assert(ClockDisplayChanged({2026, 8, 12, 23, 59},
                               {2026, 8, 13, 0, 0}));

    bool normalized = false;
    assert(NormalizeAutoShowcaseSetting(0, &normalized));
    assert(!normalized);
    assert(NormalizeAutoShowcaseSetting(1, &normalized));
    assert(normalized);
    assert(!NormalizeAutoShowcaseSetting(2, &normalized));
    assert(!NormalizeAutoShowcaseSetting(0, nullptr));

    SettingsController settings(true);
    SettingsResult setting = settings.Handle({Button::Down, Action::Click});
    assert(setting.decision == SettingsDecision::RenderFast);
    assert(!setting.auto_showcase);
    setting = settings.Handle({Button::Ok, Action::Click});
    assert(setting.decision == SettingsDecision::Save);
    assert(!setting.auto_showcase);
    assert(settings.Handle({Button::Ok, Action::LongPress}).decision ==
           SettingsDecision::Home);
    assert(settings.Handle({Button::Down, Action::LongPress}).decision ==
           SettingsDecision::Shutdown);

    DiagnosticsController diagnostics;
    DiagnosticsResult diagnostic =
        diagnostics.Handle({Button::Down, Action::Click});
    assert(diagnostic.decision == DiagnosticsDecision::RenderFast);
    assert(diagnostic.selected == 1);
    diagnostic = diagnostics.Handle({Button::Ok, Action::Click});
    assert(diagnostic.page == DiagnosticsPage::Individual);
    diagnostic = diagnostics.Handle({Button::Up, Action::Click});
    assert(diagnostic.selected == DiagnosticsController::kTestCount - 1);
    diagnostic = diagnostics.Handle({Button::Ok, Action::Click});
    assert(diagnostic.decision == DiagnosticsDecision::RunSelected);
    diagnostic = diagnostics.Handle({Button::Ok, Action::LongPress});
    assert(diagnostic.decision == DiagnosticsDecision::RenderFast);
    assert(diagnostic.page == DiagnosticsPage::Mode);
    diagnostic = diagnostics.Handle({Button::Ok, Action::Click});
    assert(diagnostic.decision == DiagnosticsDecision::RunAll);
    diagnostics.ShowSummary();
    diagnostic = diagnostics.Handle({Button::Up, Action::Click});
    assert(diagnostic.decision == DiagnosticsDecision::Home);
    assert(DiagnosticsController().Handle(
               {Button::Down, Action::LongPress}).decision ==
           DiagnosticsDecision::Shutdown);
}
