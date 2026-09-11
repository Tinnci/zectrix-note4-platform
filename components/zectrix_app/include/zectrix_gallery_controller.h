#pragma once

#include "zectrix_scene_manager.h"

namespace zectrix::app {

enum class GalleryPage : SceneId { Menu, Preview, Report };
enum class GalleryDecision : uint8_t { None, RenderFast, RenderQuality, Back, Shutdown };

class GalleryController {
public:
    void ObserveScenes(SceneSnapshot* snapshot) { scenes_.ObserveScenes(snapshot); }
    explicit GalleryController(bool automatic = false, uint32_t selected = 0);
    GalleryController(const GalleryController&) = delete;
    GalleryController& operator=(const GalleryController&) = delete;

    sdk::Status Start();
    void Stop();
    GalleryDecision Handle(const sdk::InputEvent& event);
    GalleryDecision Tick(int64_t now_us);
    // Schedule from completion, so a slow physical refresh never skips a frame.
    void Presented(int64_t now_us, bool success, bool more_frames, int64_t hold_us);

    GalleryPage page() const { return static_cast<GalleryPage>(scenes_.current()); }
    uint32_t selected() const { return scenes_.state(0); }
    uint32_t image() const { return image_; }
    uint32_t frame() const { return frame_; }

private:
    static void EnterScene(void* context, SceneId scene);
    static bool HandleScene(void* context, const SceneEvent& event);
    bool OnEvent(const SceneEvent& event);
    GalleryDecision TakeDecision();
    inline static constexpr SceneHandler kHandlers[] = {
        {EnterScene, HandleScene, nullptr},
        {EnterScene, HandleScene, nullptr},
        {EnterScene, HandleScene, nullptr},
    };
    SceneManager scenes_{kHandlers, 3, this};
    bool automatic_;
    bool run_all_ = false;
    bool dirty_ = false;
    bool quality_ = true;
    bool presented_ = false;
    bool succeeded_ = true;
    bool more_frames_ = false;
    uint32_t image_ = 0;
    uint32_t frame_ = 0;
    int64_t deadline_us_ = 0;
};

}  // namespace zectrix::app
