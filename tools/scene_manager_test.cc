#include "zectrix_gallery_controller.h"

#include <array>
#include <cassert>
#include <vector>

using namespace zectrix::app;
using zectrix::sdk::Status;
using zectrix::sdk::Button;
using Action = zectrix::sdk::InputAction;

namespace {
struct Harness {
    SceneManager* manager = nullptr;
    enum class Action { None, Push, Pop, Replace } action = Action::None;
    SceneId target = 0;
    bool conflict = false;
    bool in_event = false;
    Status result = Status::Ok;
    std::vector<int> calls;

    static void Enter(void* context, SceneId scene) {
        auto& self = *static_cast<Harness*>(context);
        assert(!self.in_event);
        assert(self.manager->Push(0) == Status::InvalidState);
        self.calls.push_back(100 + scene);
    }
    static void Exit(void* context, SceneId scene) {
        auto& self = *static_cast<Harness*>(context);
        assert(!self.in_event);
        self.calls.push_back(200 + scene);
    }
    static bool Event(void* context, const SceneEvent& event) {
        auto& self = *static_cast<Harness*>(context);
        auto& manager = *self.manager;
        const SceneId original = manager.current();
        self.in_event = true;
        assert(!manager.Dispatch(event));
        manager.Stop();
        assert(manager.current() == original);
        switch (self.action) {
            case Action::None: break;
            case Action::Push: self.result = manager.Push(self.target); break;
            case Action::Pop: self.result = manager.Pop(); break;
            case Action::Replace: self.result = manager.Replace(self.target); break;
        }
        if (self.conflict) assert(manager.Pop() == Status::Conflict);
        assert(manager.current() == original);
        self.in_event = false;
        return self.action != Action::None;
    }
};

void TestScenes() {
    std::array<SceneHandler, SceneManager::kCapacity> handlers;
    handlers.fill({Harness::Enter, Harness::Event, Harness::Exit});
    Harness h;
    SceneManager scenes(handlers.data(), handlers.size(), &h);
    h.manager = &scenes;
    assert(scenes.Start(8) == Status::InvalidArgument);
    assert(scenes.Start(0) == Status::Ok);
    assert(scenes.Start(0) == Status::InvalidState);
    assert(scenes.Push(1) == Status::InvalidState);
    assert(!scenes.Dispatch({SceneEvent::Type::Back}));
    assert(scenes.SetState(0, 7));
    assert(!scenes.SetState(8, 1));

    h.action = Harness::Action::Push;
    h.target = 1;
    h.conflict = true;
    assert(scenes.Dispatch({}));
    assert((h.calls == std::vector<int>{100, 200, 101}));
    assert(scenes.current() == 1 && scenes.depth() == 2);
    h.conflict = false;
    h.action = Harness::Action::Replace;
    h.target = 2;
    scenes.Dispatch({});
    assert(scenes.current() == 2 && scenes.depth() == 2);
    h.action = Harness::Action::None;
    assert(scenes.Dispatch({SceneEvent::Type::Back}));
    assert(scenes.current() == 0 && scenes.state(0) == 7);

    h.action = Harness::Action::Push;
    h.target = 99;
    scenes.Dispatch({});
    assert(h.result == Status::InvalidArgument && scenes.depth() == 1);
    for (SceneId scene = 1; scene < SceneManager::kCapacity; ++scene) {
        h.target = scene;
        scenes.Dispatch({});
        assert(h.result == Status::Ok);
    }
    h.target = 0;
    scenes.Dispatch({});
    assert(h.result == Status::NoMemory && scenes.depth() == SceneManager::kCapacity);
    scenes.Stop();
    const auto exits = h.calls.size();
    scenes.Stop();
    assert(h.calls.size() == exits && scenes.current() == SceneManager::kInvalidScene);
    assert(!scenes.Dispatch({}));
    assert(scenes.Start(0) == Status::Ok && scenes.state(0) == 7);
    h.action = Harness::Action::Pop;
    scenes.Dispatch({});
    assert(h.result == Status::NotFound && scenes.depth() == 1);
    scenes.Stop();
    SceneManager invalid(handlers.data(), handlers.size() + 1, &h);
    assert(invalid.Start(0) == Status::InvalidArgument);
}

void TestGallery() {
    GalleryController gallery;
    assert(gallery.Start() == Status::Ok);
    assert(gallery.page() == GalleryPage::Menu);
    assert(gallery.Tick(10000000) == GalleryDecision::None);
    assert(gallery.Handle({Button::Down, Action::Click}) == GalleryDecision::RenderFast);
    assert(gallery.selected() == 1);
    assert(gallery.Handle({Button::Ok, Action::Click}) == GalleryDecision::RenderQuality);
    assert(gallery.page() == GalleryPage::Preview && gallery.frame() == 0);
    assert(gallery.Tick(10000000) == GalleryDecision::None);
    gallery.Presented(10000000, true, true, 900000);
    assert(gallery.Tick(10899999) == GalleryDecision::None);
    assert(gallery.Tick(10900000) == GalleryDecision::RenderFast && gallery.frame() == 1);
    assert(gallery.Tick(20000000) == GalleryDecision::None);
    gallery.Presented(20000000, true, false, 2200000);
    assert(gallery.Tick(22199999) == GalleryDecision::None);
    assert(gallery.Tick(22200000) == GalleryDecision::RenderQuality);
    assert(gallery.page() == GalleryPage::Report);
    assert(gallery.Handle({Button::Ok, Action::LongPress}) == GalleryDecision::RenderQuality);
    assert(gallery.page() == GalleryPage::Menu && gallery.selected() == 1);
    gallery.Handle({Button::Ok, Action::Click});
    assert(gallery.Handle({Button::Ok, Action::LongPress}) == GalleryDecision::RenderQuality);
    assert(gallery.page() == GalleryPage::Menu);
    assert(gallery.Handle({Button::Ok, Action::LongPress}) == GalleryDecision::Back);
    assert(gallery.Handle({Button::Down, Action::LongPress}) == GalleryDecision::Shutdown);
    gallery.Handle({Button::Ok, Action::Click});
    gallery.Presented(0, true, true, 1);
    gallery.Stop();
    gallery.Stop();
    assert(gallery.Tick(1000000) == GalleryDecision::None);
    assert(gallery.Handle({Button::Ok, Action::Click}) == GalleryDecision::None);

    GalleryController all(false, 3);
    assert(all.Start() == Status::Ok);
    all.Handle({Button::Ok, Action::Click});
    for (uint32_t image = 0; image < 3; ++image) {
        assert(all.image() == image && all.frame() == 0);
        all.Presented(0, true, false, 1);
        assert(all.Tick(1) == GalleryDecision::RenderQuality);
    }
    assert(all.page() == GalleryPage::Menu && all.selected() == 3);
    all.Stop();

    GalleryController automatic(true);
    assert(automatic.Start() == Status::Ok);
    assert(automatic.page() == GalleryPage::Preview);
    automatic.Presented(0, true, false, 1);
    assert(automatic.Tick(1) == GalleryDecision::RenderQuality && automatic.image() == 1);
    automatic.Presented(2, false, true, 900000);
    assert(automatic.Tick(2) == GalleryDecision::RenderQuality);
    assert(automatic.page() == GalleryPage::Report);
    assert(automatic.Tick(100000000) == GalleryDecision::None);
    assert(automatic.Handle({Button::Down, Action::LongPress}) == GalleryDecision::Shutdown);
    assert(automatic.Handle({Button::Up, Action::Click}) == GalleryDecision::Back);
    automatic.Stop();
}
}

int main() {
    TestScenes();
    TestGallery();
}
