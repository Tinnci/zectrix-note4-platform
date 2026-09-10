#include "zectrix_launcher_controller.h"

#include <cstring>

namespace zectrix::app {

LauncherController::LauncherController(const ApplicationCatalog& catalog) : catalog_(catalog) {
    for (std::size_t i = 0; i < catalog_.menu_size(); ++i) {
        const auto* entry = catalog_.MenuAt(i);
        if (entry->id && std::strcmp(entry->id, "reader") == 0) {
            reader_ = entry;
            break;
        }
    }
}

bool LauncherDateChanged(const time::ClockSnapshot& before, const time::ClockSnapshot& after) {
    const bool before_valid = before.source != time::ClockSource::Uptime && time::IsValid(before.value);
    const bool after_valid = after.source != time::ClockSource::Uptime && time::IsValid(after.value);
    return before_valid != after_valid || (after_valid &&
        (before.value.year != after.value.year || before.value.month != after.value.month ||
         before.value.day != after.value.day));
}

std::size_t LauncherController::Count(LauncherScene scene) const {
    if (scene != LauncherScene::Home && scene != LauncherScene::Tools) return 0;
    std::size_t home = 0, tools = 0;
    for (std::size_t i = 0; i < catalog_.menu_size(); ++i) {
        if (catalog_.MenuPresentationAt(i).on_home) ++home;
        else ++tools;
    }
    return scene == LauncherScene::Home ? tile_offset() + home + (tools ? 1 : 0) : tools;
}

std::size_t LauncherController::count() const { return Count(scene()); }

LauncherEntry LauncherController::EntryAt(std::size_t index) const {
    if (index >= count()) return {};
    const bool home = scene() == LauncherScene::Home;
    if (home && has_reading_overview()) {
        if (index == 0) return {reader_->id, "CONTINUE READING", ApplicationIcon::Book};
        --index;
    }
    for (std::size_t i = 0; i < catalog_.menu_size(); ++i) {
        const auto presentation = catalog_.MenuPresentationAt(i);
        if (presentation.on_home != home) continue;
        if (index-- != 0) continue;
        const auto* entry = catalog_.MenuAt(i);
        return {entry->id, entry->display_name, presentation.icon};
    }
    return {nullptr, "TOOLS", ApplicationIcon::Tools};
}

sdk::Status LauncherController::Start(LauncherSelection selection) {
    if (scenes_.depth()) return sdk::Status::InvalidState;
    scenes_.SetState(0, selection.home);
    scenes_.SetState(1, selection.tools < Count(LauncherScene::Tools) ? selection.tools : 0);
    action_ = {};
    return scenes_.Start(static_cast<SceneId>(LauncherScene::Home));
}

void LauncherController::Stop() {
    scenes_.Stop();
    action_ = {};
    dirty_ = quality_ = false;
}

void LauncherController::Enter(void* context, SceneId scene) {
    auto& self = *static_cast<LauncherController*>(context);
    if (self.scenes_.state(scene) >= self.count()) self.scenes_.SetState(scene, 0);
    self.Invalidate(true);
}

bool LauncherController::Event(void* context, const SceneEvent& event) {
    auto& self = *static_cast<LauncherController*>(context);
    if (event.type != SceneEvent::Type::Input) return false;
    const auto& input = event.input;
    if (input.button == sdk::Button::Down && input.action == sdk::InputAction::LongPress) {
        self.action_ = {LauncherDecision::Shutdown};
        return true;
    }
    if (input.action != sdk::InputAction::Click || self.count() == 0) return false;
    const auto selected = self.selected();
    if (input.button == sdk::Button::Up || input.button == sdk::Button::Down) {
        const auto next = (selected + (input.button == sdk::Button::Up ? self.count() - 1 : 1)) % self.count();
        self.scenes_.SetState(self.scenes_.current(), static_cast<uint32_t>(next));
        if (next != selected) self.Invalidate(self.scene() == LauncherScene::Home &&
            self.TilePage(selected) != self.TilePage(next));
    } else if (input.button == sdk::Button::Ok) {
        const auto entry = self.EntryAt(selected);
        if (entry.id) self.action_ = {self.overview_selected()
            ? LauncherDecision::ContinueReading : LauncherDecision::OpenSelected, entry.id};
        else self.scenes_.Push(static_cast<SceneId>(LauncherScene::Tools));
    }
    return true;
}

LauncherResult LauncherController::Handle(const sdk::InputEvent& input) {
    const bool back = input.button == sdk::Button::Ok && input.action == sdk::InputAction::LongPress;
    scenes_.Dispatch({back ? SceneEvent::Type::Back : SceneEvent::Type::Input, input});
    return Tick();
}

LauncherResult LauncherController::Tick() {
    if (!scenes_.depth()) return {};
    const auto action = action_;
    action_ = {};
    if (action.decision != LauncherDecision::None) return action;
    if (!dirty_) return {};
    const auto decision = quality_ ? LauncherDecision::RenderQuality : LauncherDecision::RenderFast;
    dirty_ = quality_ = false;
    return {decision};
}

}  // namespace zectrix::app
