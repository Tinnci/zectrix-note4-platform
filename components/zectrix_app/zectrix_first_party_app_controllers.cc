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
        LauncherDecision decision = LauncherDecision::RunLegacy;
        if (selected_ == 0) decision = LauncherDecision::OpenClock;
        if (selected_ == 1) decision = LauncherDecision::OpenSettings;
        return {decision, selected_};
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

SettingsResult SettingsController::Handle(const input::InputEvent& event) {
    if (event.button == input::Button::Down &&
        event.action == input::Action::LongPress) {
        return {SettingsDecision::Shutdown, auto_showcase_};
    }
    if (event.button == input::Button::Ok &&
        event.action == input::Action::LongPress) {
        return {SettingsDecision::Home, auto_showcase_};
    }
    if (event.action != input::Action::Click) {
        return {SettingsDecision::None, auto_showcase_};
    }
    if (event.button == input::Button::Up ||
        event.button == input::Button::Down) {
        auto_showcase_ = !auto_showcase_;
        return {SettingsDecision::RenderFast, auto_showcase_};
    }
    if (event.button == input::Button::Ok) {
        return {SettingsDecision::Save, auto_showcase_};
    }
    return {SettingsDecision::None, auto_showcase_};
}

bool NormalizeAutoShowcaseSetting(uint32_t stored, bool* value) {
    if (value == nullptr || stored > 1) return false;
    *value = stored == 1;
    return true;
}

}  // namespace zectrix::app
