#pragma once

#include "zectrix/sdk/application.h"

namespace zectrix::app {

inline constexpr std::size_t kMaxInputBurst = 16;

// Poll only already queued input. Callbacks and physical display completion
// stay on the foreground owner, including the maintenance hook in Poll.
template <typename Poll>
sdk::Status DispatchInputBurst(sdk::ApplicationRuntime& runtime,
                               sdk::InputEvent event, Poll poll) {
    const auto generation = runtime.foreground_generation();
    for (std::size_t count = 0; count < kMaxInputBurst; ++count) {
        const bool direction = event.action == sdk::InputAction::Click &&
            (event.button == sdk::Button::Up || event.button == sdk::Button::Down);
        const sdk::Status result = runtime.DispatchInput(event);
        if (runtime.state() != sdk::LifecycleState::Active) return result;
        if (!sdk::IsOk(result) || !direction ||
            runtime.foreground_generation() != generation) {
            const sdk::Status rendered = runtime.Step();
            return sdk::IsOk(rendered) ? result : rendered;
        }
        if (count + 1 == kMaxInputBurst || !poll(&event)) {
            // Give streamed pagination and timers a slice even under a steady
            // input backlog, then compose their changes into the same frame.
            return runtime.Idle();
        }
    }
    return sdk::Status::Ok;
}

}  // namespace zectrix::app
