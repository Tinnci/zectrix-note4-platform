#include "zectrix_first_party_app_controllers.h"

#include <cassert>

int main() {
    using namespace zectrix::app;
    using zectrix::input::Action;
    using zectrix::input::Button;
    using zectrix::input::InputEvent;

    LauncherController launcher;
    LauncherResult result = launcher.Handle({Button::Ok, Action::Click});
    assert(result.decision == LauncherDecision::OpenClock);
    assert(result.selected == 0);

    result = launcher.Handle({Button::Down, Action::Click});
    assert(result.decision == LauncherDecision::RenderFast);
    assert(result.selected == 1);
    result = launcher.Handle({Button::Ok, Action::Click});
    assert(result.decision == LauncherDecision::OpenSettings);
    assert(result.selected == 1);

    result = launcher.Handle({Button::Down, Action::Click});
    assert(result.selected == 2);
    result = launcher.Handle({Button::Ok, Action::Click});
    assert(result.decision == LauncherDecision::RunLegacy);

    result = launcher.Handle({Button::Up, Action::Click});
    assert(result.selected == 1);
    result = launcher.Handle({Button::Up, Action::Click});
    assert(result.selected == 0);
    result = launcher.Handle({Button::Up, Action::Click});
    assert(result.selected == LauncherController::kItemCount - 1);
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
}
