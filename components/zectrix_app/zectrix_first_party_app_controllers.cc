#include "zectrix_first_party_app_controllers.h"

namespace zectrix::app {

ConnectivityDecision HandleConnectivityInput(const sdk::InputEvent& event) {
    if (event.button == sdk::Button::Down &&
        event.action == sdk::InputAction::LongPress) {
        return ConnectivityDecision::Shutdown;
    }
    if (event.button == sdk::Button::Ok &&
        event.action == sdk::InputAction::LongPress) {
        return ConnectivityDecision::Home;
    }
    if (event.button == sdk::Button::Up &&
        event.action == sdk::InputAction::LongPress) {
        return ConnectivityDecision::ClearBonds;
    }
    if (event.action != sdk::InputAction::Click) {
        return ConnectivityDecision::None;
    }
    if (event.button == sdk::Button::Ok) {
        return ConnectivityDecision::StartPairing;
    }
    if (event.button == sdk::Button::Up) {
        return ConnectivityDecision::FetchResource;
    }
    return ConnectivityDecision::None;
}

ClockDecision HandleClockInput(const sdk::InputEvent& event) {
    if (event.button == sdk::Button::Down &&
        event.action == sdk::InputAction::LongPress) {
        return ClockDecision::Shutdown;
    }
    if (event.button == sdk::Button::Ok &&
        event.action == sdk::InputAction::LongPress) {
        return ClockDecision::Home;
    }
    return ClockDecision::None;
}

bool ClockDisplayChanged(const ClockMinute& displayed,
                         const ClockMinute& current) {
    return displayed.year != current.year || displayed.month != current.month ||
           displayed.day != current.day || displayed.hour != current.hour ||
           displayed.minute != current.minute;
}

SettingsResult SettingsController::Handle(const sdk::InputEvent& event) {
    if (event.button == sdk::Button::Down &&
        event.action == sdk::InputAction::LongPress) {
        return {SettingsDecision::Shutdown, auto_showcase_};
    }
    if (event.button == sdk::Button::Ok &&
        event.action == sdk::InputAction::LongPress) {
        return {SettingsDecision::Home, auto_showcase_};
    }
    if (event.action != sdk::InputAction::Click) {
        return {SettingsDecision::None, auto_showcase_};
    }
    if (event.button == sdk::Button::Up ||
        event.button == sdk::Button::Down) {
        auto_showcase_ = !auto_showcase_;
        return {SettingsDecision::RenderFast, auto_showcase_};
    }
    if (event.button == sdk::Button::Ok) {
        return {SettingsDecision::Save, auto_showcase_};
    }
    return {SettingsDecision::None, auto_showcase_};
}

bool NormalizeAutoShowcaseSetting(uint32_t stored, bool* value) {
    if (value == nullptr || stored > 1) return false;
    *value = stored == 1;
    return true;
}

DiagnosticsResult DiagnosticsController::Handle(
    const sdk::InputEvent& event) {
    if (event.button == sdk::Button::Down &&
        event.action == sdk::InputAction::LongPress) {
        return {DiagnosticsDecision::Shutdown, page_, selected_};
    }
    if (event.button == sdk::Button::Ok &&
        event.action == sdk::InputAction::LongPress) {
        if (page_ == DiagnosticsPage::Individual) {
            page_ = DiagnosticsPage::Mode;
            selected_ = 0;
            return {DiagnosticsDecision::RenderFast, page_, selected_};
        }
        return {DiagnosticsDecision::Home, page_, selected_};
    }
    if (page_ == DiagnosticsPage::Summary &&
        event.action == sdk::InputAction::Click) {
        return {DiagnosticsDecision::Home, page_, selected_};
    }
    if (event.action != sdk::InputAction::Click) {
        return {DiagnosticsDecision::None, page_, selected_};
    }
    if (event.button == sdk::Button::Up ||
        event.button == sdk::Button::Down) {
        const std::size_t count = page_ == DiagnosticsPage::Mode
                                      ? 2 : kTestCount;
        selected_ = event.button == sdk::Button::Up
                        ? (selected_ + count - 1) % count
                        : (selected_ + 1) % count;
        return {DiagnosticsDecision::RenderFast, page_, selected_};
    }
    if (event.button == sdk::Button::Ok) {
        if (page_ == DiagnosticsPage::Mode && selected_ == 0) {
            return {DiagnosticsDecision::RunAll, page_, selected_};
        }
        if (page_ == DiagnosticsPage::Mode) {
            page_ = DiagnosticsPage::Individual;
            selected_ = 0;
            return {DiagnosticsDecision::RenderFast, page_, selected_};
        }
        return {DiagnosticsDecision::RunSelected, page_, selected_};
    }
    return {DiagnosticsDecision::None, page_, selected_};
}

}  // namespace zectrix::app
