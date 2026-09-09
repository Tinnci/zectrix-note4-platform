#include "zectrix_scene_manager.h"

namespace zectrix::app {

sdk::Status SceneManager::Start(SceneId root) {
    if (busy_ || depth_) return sdk::Status::InvalidState;
    if (!handlers_ || count_ == 0 || count_ > kCapacity || root >= count_)
        return sdk::Status::InvalidArgument;
    busy_ = true;
    stack_[depth_++] = root;
    Enter();
    busy_ = false;
    return sdk::Status::Ok;
}

void SceneManager::Enter() {
    if (handlers_[current()].enter) handlers_[current()].enter(context_, current());
}

void SceneManager::Exit() {
    if (handlers_[current()].exit) handlers_[current()].exit(context_, current());
}

bool SceneManager::Dispatch(const SceneEvent& event) {
    if (busy_ || !depth_) return false;
    busy_ = in_event_ = true;
    const auto handler = handlers_[current()].event;
    bool consumed = handler && handler(context_, event);
    if (!consumed && event.type == SceneEvent::Type::Back && depth_ > 1) {
        Pop();
        consumed = true;
    }
    in_event_ = false;
    consumed = consumed || pending_ != Transition::None;
    Apply();
    busy_ = false;
    return consumed;
}

sdk::Status SceneManager::Request(Transition transition, SceneId scene) {
    if (!in_event_) return sdk::Status::InvalidState;
    if (pending_ != Transition::None) return sdk::Status::Conflict;
    if (transition == Transition::Pop) {
        if (depth_ < 2) return sdk::Status::NotFound;
    } else {
        if (scene >= count_) return sdk::Status::InvalidArgument;
        if (transition == Transition::Push && depth_ == kCapacity)
            return sdk::Status::NoMemory;
    }
    pending_ = transition;
    target_ = scene;
    return sdk::Status::Ok;
}

sdk::Status SceneManager::Push(SceneId scene) { return Request(Transition::Push, scene); }
sdk::Status SceneManager::Replace(SceneId scene) { return Request(Transition::Replace, scene); }
sdk::Status SceneManager::Pop() { return Request(Transition::Pop, kInvalidScene); }

void SceneManager::Apply() {
    const Transition transition = pending_;
    pending_ = Transition::None;
    if (transition == Transition::None) return;
    Exit();
    if (transition == Transition::Pop) --depth_;
    else if (transition == Transition::Push) stack_[depth_++] = target_;
    else stack_[depth_ - 1] = target_;
    Enter();
}

void SceneManager::Stop() {
    if (busy_ || !depth_) return;
    busy_ = true;
    // Suspended parents already received Exit when the child was pushed.
    Exit();
    depth_ = 0;
    pending_ = Transition::None;
    busy_ = false;
}

uint32_t SceneManager::state(SceneId scene) const {
    return scene < count_ && scene < kCapacity ? states_[scene] : 0;
}

bool SceneManager::SetState(SceneId scene, uint32_t value) {
    if (scene >= count_ || scene >= kCapacity) return false;
    states_[scene] = value;
    return true;
}

}  // namespace zectrix::app
