#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "zectrix/sdk/input.h"
#include "zectrix/sdk/status.h"
#include "zectrix_navigation.h"

namespace zectrix::app {

using SceneId = uint8_t;

struct SceneEvent {
    enum class Type : uint8_t { Input, Back, Tick };
    Type type = Type::Tick;
    sdk::InputEvent input{};
    int64_t now_us = 0;
};

struct SceneHandler {
    void (*enter)(void* context, SceneId scene) = nullptr;
    bool (*event)(void* context, const SceneEvent& event) = nullptr;
    void (*exit)(void* context, SceneId scene) = nullptr;
};

// App-private navigation stays on the application owner task. Handler tables
// and context must outlive the manager; no application IDs or heap are needed.
class SceneManager {
public:
    static constexpr std::size_t kCapacity = 8;
    static constexpr SceneId kInvalidScene = 0xff;

    SceneManager(const SceneHandler* handlers, std::size_t count, void* context)
        : handlers_(handlers), count_(count), context_(context) {}

    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    sdk::Status Start(SceneId root);
    bool Dispatch(const SceneEvent& event);
    void Stop();

    // Only event callbacks may navigate. Enter/exit cannot recursively switch.
    sdk::Status Push(SceneId scene);
    sdk::Status Replace(SceneId scene);
    sdk::Status Pop();
    SceneId current() const { return depth_ ? stack_[depth_ - 1] : kInvalidScene; }
    std::size_t depth() const { return depth_; }
    uint32_t state(SceneId scene) const;
    bool SetState(SceneId scene, uint32_t value);

private:
    enum class Transition : uint8_t { None, Push, Replace, Pop };
    sdk::Status Request(Transition transition, SceneId scene);
    void Apply();
    void Enter();
    void Exit();

    const SceneHandler* handlers_;
    std::size_t count_;
    void* context_;
    std::array<SceneId, kCapacity> stack_{};
    std::array<uint32_t, kCapacity> states_{};
    std::size_t depth_ = 0;
    bool busy_ = false;
    bool in_event_ = false;
    Transition pending_ = Transition::None;
    SceneId target_ = kInvalidScene;
};

}  // namespace zectrix::app
