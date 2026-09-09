#include "zectrix_gallery_controller.h"

#include <algorithm>

namespace zectrix::app {
namespace {
constexpr SceneId Id(GalleryPage page) { return static_cast<SceneId>(page); }
}

GalleryController::GalleryController(bool automatic, uint32_t selected)
    : automatic_(automatic) {
    scenes_.SetState(Id(GalleryPage::Menu), selected < 4 ? selected : 0);
}

sdk::Status GalleryController::Start() {
    const auto result = scenes_.Start(Id(automatic_ ? GalleryPage::Preview : GalleryPage::Menu));
    dirty_ = false;
    return result;
}

void GalleryController::EnterScene(void* context, SceneId scene) {
    auto& self = *static_cast<GalleryController*>(context);
    self.dirty_ = self.quality_ = true;
    if (scene == Id(GalleryPage::Preview)) {
        self.frame_ = 0;
        self.presented_ = false;
        self.succeeded_ = true;
        self.more_frames_ = false;
    }
}

bool GalleryController::HandleScene(void* context, const SceneEvent& event) {
    return static_cast<GalleryController*>(context)->OnEvent(event);
}

GalleryDecision GalleryController::Handle(const sdk::InputEvent& input) {
    if (input.button == sdk::Button::Down && input.action == sdk::InputAction::LongPress)
        return GalleryDecision::Shutdown;
    if (automatic_ && input.action == sdk::InputAction::Click) return GalleryDecision::Home;
    const bool back = input.button == sdk::Button::Ok && input.action == sdk::InputAction::LongPress;
    const SceneEvent event{back ? SceneEvent::Type::Back : SceneEvent::Type::Input, input, 0};
    if (!scenes_.Dispatch(event) && back) return GalleryDecision::Home;
    return TakeDecision();
}

GalleryDecision GalleryController::Tick(int64_t now_us) {
    scenes_.Dispatch({SceneEvent::Type::Tick, {}, now_us});
    return TakeDecision();
}

void GalleryController::Presented(int64_t now_us, bool success, bool more_frames, int64_t hold_us) {
    if (page() != GalleryPage::Preview) return;
    presented_ = true;
    succeeded_ = success;
    more_frames_ = more_frames;
    deadline_us_ = now_us + (success ? std::max<int64_t>(0, hold_us) : 0);
}

GalleryDecision GalleryController::TakeDecision() {
    if (!dirty_) return GalleryDecision::None;
    dirty_ = false;
    return quality_ ? GalleryDecision::RenderQuality : GalleryDecision::RenderFast;
}

bool GalleryController::OnEvent(const SceneEvent& event) {
    if (event.type == SceneEvent::Type::Tick) {
        if (page() != GalleryPage::Preview || !presented_ || event.now_us < deadline_us_) return false;
        if (!succeeded_) {
            scenes_.Replace(Id(GalleryPage::Report));
        } else if (more_frames_) {
            ++frame_;
            presented_ = false;
            dirty_ = true;
            quality_ = false;
        } else if (automatic_ || (run_all_ && image_ < 2)) {
            image_ = (image_ + 1) % 3;
            scenes_.Replace(Id(GalleryPage::Preview));
        } else if (run_all_) {
            scenes_.Pop();
        } else {
            scenes_.Replace(Id(GalleryPage::Report));
        }
        return true;
    }
    if (event.type != SceneEvent::Type::Input || event.input.action != sdk::InputAction::Click) return false;
    if (page() == GalleryPage::Menu) {
        if (event.input.button == sdk::Button::Up || event.input.button == sdk::Button::Down) {
            scenes_.SetState(Id(GalleryPage::Menu),
                (selected() + (event.input.button == sdk::Button::Up ? 3 : 1)) % 4);
            dirty_ = true;
            quality_ = false;
        } else if (event.input.button == sdk::Button::Ok) {
            run_all_ = selected() == 3;
            image_ = run_all_ ? 0 : selected();
            scenes_.Push(Id(GalleryPage::Preview));
        }
        return true;
    }
    if (event.input.button == sdk::Button::Ok) {
        if (page() == GalleryPage::Preview) scenes_.Replace(Id(GalleryPage::Report));
        else scenes_.Pop();
        return true;
    }
    return false;
}

}  // namespace zectrix::app
