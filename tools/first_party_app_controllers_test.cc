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
    assert(result.decision == LauncherDecision::RunLegacy);
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
}
