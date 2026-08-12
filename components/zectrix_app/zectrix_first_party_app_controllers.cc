#include "zectrix_first_party_app_controllers.h"

namespace zectrix::app {

LauncherResult LauncherController::Handle(const input::InputEvent& event) {
    if (event.button == input::Button::Down &&
        event.action == input::Action::LongPress) {
        return {LauncherDecision::Shutdown, selected_};
    }
    if (event.action != input::Action::Click) {
        return {LauncherDecision::None, selected_};
    }
    if (event.button == input::Button::Up) {
        selected_ = (selected_ + kItemCount - 1) % kItemCount;
        return {LauncherDecision::RenderFast, selected_};
    }
    if (event.button == input::Button::Down) {
        selected_ = (selected_ + 1) % kItemCount;
        return {LauncherDecision::RenderFast, selected_};
    }
    if (event.button == input::Button::Ok) {
        return {selected_ == 0 ? LauncherDecision::OpenClock
                               : LauncherDecision::RunLegacy,
                selected_};
    }
    return {LauncherDecision::None, selected_};
}

ClockDecision HandleClockInput(const input::InputEvent& event) {
    if (event.button == input::Button::Down &&
        event.action == input::Action::LongPress) {
        return ClockDecision::Shutdown;
    }
    if (event.button == input::Button::Ok &&
        event.action == input::Action::LongPress) {
        return ClockDecision::Home;
    }
    return ClockDecision::None;
}

}  // namespace zectrix::app
