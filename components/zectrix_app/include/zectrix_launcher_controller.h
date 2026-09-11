#pragma once

#include "zectrix_application_catalog.h"
#include "zectrix_scene_manager.h"
#include "zectrix_time_service.h"

namespace zectrix::app {

enum class LauncherScene : uint8_t { Home, Tools };
enum class LauncherDecision : uint8_t {
    None, RenderFast, RenderQuality, OpenSelected, ContinueReading, Shutdown
};

struct LauncherSelection {
    uint32_t home = 0;
    uint32_t tools = 0;
    LauncherScene scene = LauncherScene::Home;
};

struct LauncherEntry {
    const char* id = nullptr;
    const char* label = nullptr;
    ApplicationIcon icon = ApplicationIcon::App;
    i18n::Text label_text = i18n::Text::None;
};

struct LauncherResult {
    LauncherDecision decision = LauncherDecision::None;
    const char* target = nullptr;
};

bool LauncherDateChanged(const time::ClockSnapshot& before, const time::ClockSnapshot& after);

class LauncherController {
public:
    void ObserveScenes(SceneSnapshot* snapshot) { scenes_.ObserveScenes(snapshot); }
    static constexpr std::size_t kTilesPerPage = 6;
    explicit LauncherController(const ApplicationCatalog& catalog);
    sdk::Status Start(LauncherSelection selection = {});
    LauncherResult Handle(const sdk::InputEvent& input);
    LauncherResult Tick();
    void Stop();
    void Invalidate(bool quality = false) { dirty_ = true; quality_ = quality_ || quality; }
    void Presented(bool success) { if (!success) Invalidate(true); }
    LauncherScene scene() const { return static_cast<LauncherScene>(scenes_.current()); }
    std::size_t selected() const { return scenes_.state(scenes_.current()); }
    std::size_t count() const;
    bool has_reading_overview() const { return reader_ != nullptr; }
    bool overview_selected() const {
        return scene() == LauncherScene::Home && has_reading_overview() && selected() == 0;
    }
    std::size_t tile_offset() const { return has_reading_overview() ? 1 : 0; }
    std::size_t tile_page() const { return TilePage(selected()); }
    LauncherEntry EntryAt(std::size_t index) const;
    LauncherSelection selection() const { return {scenes_.state(0), scenes_.state(1), scene()}; }

private:
    std::size_t Count(LauncherScene scene) const;
    std::size_t TilePage(std::size_t selection) const {
        return selection < tile_offset() ? 0 : (selection - tile_offset()) / kTilesPerPage;
    }
    static void Enter(void* context, SceneId scene);
    static bool Event(void* context, const SceneEvent& event);
    inline static constexpr SceneHandler kHandlers[] = {{Enter, Event, nullptr}, {Enter, Event, nullptr}};
    const ApplicationCatalog& catalog_;
    const sdk::ApplicationDescriptor* reader_ = nullptr;
    SceneManager scenes_{kHandlers, std::size(kHandlers), this};
    LauncherResult action_{};
    bool dirty_ = false;
    bool quality_ = false;
};

}  // namespace zectrix::app
