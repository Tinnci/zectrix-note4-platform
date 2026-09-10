#include "zectrix_first_party_app_controllers.h"

namespace zectrix::app {
namespace {
constexpr SceneId Id(ConnectivityPage page) { return static_cast<SceneId>(page); }
constexpr SceneId Id(DiagnosticsPage page) { return static_cast<SceneId>(page); }
}

sdk::Status ConnectivityController::Start() {
    return scenes_.Start(Id(ConnectivityPage::Actions));
}

void ConnectivityController::Stop() {
    scenes_.Stop();
    action_ = ConnectivityDecision::None;
    dirty_ = quality_ = false;
}

void ConnectivityController::Enter(void* context, SceneId scene) {
    auto& self = *static_cast<ConnectivityController*>(context);
    if (scene == Id(ConnectivityPage::Forget)) self.scenes_.SetState(scene, 0);
    self.dirty_ = self.quality_ = true;
}

bool ConnectivityController::Event(void* context, const SceneEvent& event) {
    auto& self = *static_cast<ConnectivityController*>(context);
    if (event.type != SceneEvent::Type::Input) return false;
    const auto key = MapNavigation(event.input);
    const bool actions = self.page() == ConnectivityPage::Actions;
    if (key == Navigation::Previous || key == Navigation::Next) {
        self.scenes_.SetState(self.scenes_.current(), MoveSelection(self.selected(), actions ? 3 : 2, key));
        self.dirty_ = true;
    } else if (key == Navigation::Confirm) {
        if (!actions) {
            if (self.selected() == 1) self.action_ = ConnectivityDecision::ClearBonds;
            self.scenes_.Pop();
        } else if (self.selected() == 2) {
            self.scenes_.Push(Id(ConnectivityPage::Forget));
        } else {
            self.action_ = self.selected() == 0 ? ConnectivityDecision::StartPairing
                                              : ConnectivityDecision::FetchResource;
        }
    } else return false;
    return true;
}

ConnectivityDecision ConnectivityController::Handle(const sdk::InputEvent& event) {
    if (!scenes_.depth()) return ConnectivityDecision::None;
    const auto key = MapNavigation(event);
    if (key == Navigation::Shutdown) return ConnectivityDecision::Shutdown;
    const bool back = key == Navigation::Back;
    if (!scenes_.Dispatch({back ? SceneEvent::Type::Back : SceneEvent::Type::Input, event}) && back)
        return ConnectivityDecision::Back;
    return Tick();
}

ConnectivityDecision ConnectivityController::Tick() {
    if (!scenes_.depth()) return ConnectivityDecision::None;
    const auto result = action_ != ConnectivityDecision::None ? action_ : !dirty_ ? ConnectivityDecision::None :
        quality_ ? ConnectivityDecision::RenderQuality : ConnectivityDecision::RenderFast;
    action_ = ConnectivityDecision::None;
    dirty_ = quality_ = false;
    return result;
}

bool ClockDisplayChanged(const ClockMinute& displayed,
                         const ClockMinute& current) {
    return displayed.year != current.year || displayed.month != current.month ||
           displayed.day != current.day || displayed.hour != current.hour ||
           displayed.minute != current.minute;
}

sdk::Status SettingsController::Start() { return Start(auto_showcase_, language_); }

sdk::Status SettingsController::Start(bool auto_showcase, i18n::Language language) {
    if (scenes_.depth()) return sdk::Status::InvalidState;
    auto_showcase_ = auto_showcase;
    language_ = i18n::Supports(language) ? language : i18n::DefaultLanguage();
    save_failed_ = false;
    return scenes_.Start(static_cast<SceneId>(SettingsPage::Options));
}

void SettingsController::Stop() {
    scenes_.Stop();
    action_ = SettingsDecision::None;
    dirty_ = quality_ = false;
}

void SettingsController::Enter(void* context, SceneId) {
    auto& self = *static_cast<SettingsController*>(context);
    self.dirty_ = self.quality_ = true;
}

bool SettingsController::Event(void* context, const SceneEvent& event) {
    auto& self = *static_cast<SettingsController*>(context);
    if (event.type != SceneEvent::Type::Input) return false;
    const auto key = MapNavigation(event.input);
    if (key == Navigation::Previous || key == Navigation::Next) {
        const auto count = self.page() == SettingsPage::Options ? self.option_count() : i18n::LanguageCount();
        self.scenes_.SetState(self.scenes_.current(), MoveSelection(self.selected(), count, key));
        self.dirty_ = true;
    } else if (key == Navigation::Confirm) {
        if (self.page() == SettingsPage::Language) {
            self.language_ = static_cast<i18n::Language>(self.selected());
            self.action_ = SettingsDecision::SaveLanguage;
            self.dirty_ = self.quality_ = true;
        } else if (self.option_count() > 1 && self.selected() == 0) {
            self.scenes_.SetState(static_cast<SceneId>(SettingsPage::Language), static_cast<uint32_t>(self.language_));
            self.scenes_.Push(static_cast<SceneId>(SettingsPage::Language));
        } else {
            if (!self.save_failed_) self.auto_showcase_ = !self.auto_showcase_;
            self.action_ = SettingsDecision::Save;
            self.dirty_ = true;
        }
    } else return false;
    return true;
}

SettingsResult SettingsController::Handle(const sdk::InputEvent& event) {
    if (!scenes_.depth()) return {};
    const auto key = MapNavigation(event);
    if (key == Navigation::Shutdown) return {SettingsDecision::Shutdown, auto_showcase_, language_};
    const bool back = key == Navigation::Back;
    if (!scenes_.Dispatch({back ? SceneEvent::Type::Back : SceneEvent::Type::Input, event}) && back)
        return {SettingsDecision::Back, auto_showcase_, language_};
    return Tick();
}

SettingsResult SettingsController::Tick() {
    if (!scenes_.depth()) return {};
    const auto decision = action_ != SettingsDecision::None ? action_ : !dirty_ ? SettingsDecision::None :
        quality_ ? SettingsDecision::RenderQuality : SettingsDecision::RenderFast;
    action_ = SettingsDecision::None;
    dirty_ = quality_ = false;
    return {decision, auto_showcase_, language_};
}

bool NormalizeAutoShowcaseSetting(uint32_t stored, bool* value) {
    if (value == nullptr || stored > 1) return false;
    *value = stored == 1;
    return true;
}

sdk::Status DiagnosticsController::Start() { return scenes_.Start(Id(DiagnosticsPage::Mode)); }

void DiagnosticsController::Stop() {
    scenes_.Stop();
    action_ = DiagnosticsDecision::None;
    dirty_ = quality_ = finish_requested_ = false;
}

std::size_t DiagnosticsController::selected() const {
    const bool individual = page() == DiagnosticsPage::Individual ||
        (page() == DiagnosticsPage::Running && !run_all_);
    return scenes_.state(Id(individual ? DiagnosticsPage::Individual : DiagnosticsPage::Mode));
}

void DiagnosticsController::Enter(void* context, SceneId) {
    auto& self = *static_cast<DiagnosticsController*>(context);
    self.dirty_ = self.quality_ = true;
}

bool DiagnosticsController::Event(void* context, const SceneEvent& event) {
    auto& self = *static_cast<DiagnosticsController*>(context);
    if (event.type == SceneEvent::Type::Tick && self.page() == DiagnosticsPage::Running && self.finish_requested_) {
        self.finish_requested_ = false;
        if (self.run_all_ && !self.cancelled_) self.scenes_.Replace(Id(DiagnosticsPage::Summary));
        else self.scenes_.Pop();
        return true;
    }
    if (event.type != SceneEvent::Type::Input) return false;
    const auto key = MapNavigation(event.input);
    if (self.page() == DiagnosticsPage::Summary) {
        if (key != Navigation::Confirm) return false;
        self.scenes_.Pop();
        return true;
    }
    if (self.page() == DiagnosticsPage::Running) return false;
    if (key == Navigation::Previous || key == Navigation::Next) {
        const auto count = self.page() == DiagnosticsPage::Mode ? 2 : kTestCount;
        self.scenes_.SetState(self.scenes_.current(), MoveSelection(self.selected(), count, key));
        self.dirty_ = true;
    } else if (key == Navigation::Confirm) {
        if (self.page() == DiagnosticsPage::Mode && self.selected() == 1) {
            self.scenes_.Push(Id(DiagnosticsPage::Individual));
        } else {
            self.run_all_ = self.page() == DiagnosticsPage::Mode;
            self.action_ = self.run_all_ ? DiagnosticsDecision::RunAll : DiagnosticsDecision::RunSelected;
            self.scenes_.Push(Id(DiagnosticsPage::Running));
        }
    } else return false;
    return true;
}

DiagnosticsResult DiagnosticsController::Handle(const sdk::InputEvent& event) {
    if (!scenes_.depth()) return {};
    const auto key = MapNavigation(event);
    if (key == Navigation::Shutdown) return {DiagnosticsDecision::Shutdown, page(), selected()};
    const bool back = key == Navigation::Back;
    if (!scenes_.Dispatch({back ? SceneEvent::Type::Back : SceneEvent::Type::Input, event}) && back)
        return {DiagnosticsDecision::Back, page(), selected()};
    return Tick();
}

DiagnosticsResult DiagnosticsController::FinishRun(bool cancelled) {
    if (page() != DiagnosticsPage::Running) return {};
    finish_requested_ = true;
    cancelled_ = cancelled;
    scenes_.Dispatch({SceneEvent::Type::Tick});
    return Tick();
}

DiagnosticsResult DiagnosticsController::Tick() {
    if (!scenes_.depth()) return {};
    const auto decision = action_ != DiagnosticsDecision::None ? action_ : !dirty_ ? DiagnosticsDecision::None :
        quality_ ? DiagnosticsDecision::RenderQuality : DiagnosticsDecision::RenderFast;
    action_ = DiagnosticsDecision::None;
    dirty_ = quality_ = false;
    return {decision, page(), selected()};
}

}  // namespace zectrix::app
