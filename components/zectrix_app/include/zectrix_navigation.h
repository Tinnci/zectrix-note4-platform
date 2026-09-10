#pragma once

#include <cstddef>

#include "zectrix/sdk/input.h"

namespace zectrix::app {

enum class Navigation : uint8_t { None, Previous, Next, Confirm, Back, Shutdown };

// Keep product gestures independent of GPIO sampling and individual scenes.
constexpr Navigation MapNavigation(const sdk::InputEvent& input) {
    if (input.action == sdk::InputAction::LongPress) {
        if (input.button == sdk::Button::Down) return Navigation::Shutdown;
        if (input.button == sdk::Button::Ok) return Navigation::Back;
        return Navigation::None;
    }
    if (input.action != sdk::InputAction::Click) return Navigation::None;
    switch (input.button) {
        case sdk::Button::Up: return Navigation::Previous;
        case sdk::Button::Down: return Navigation::Next;
        case sdk::Button::Ok: return Navigation::Confirm;
    }
    return Navigation::None;
}

constexpr std::size_t MoveSelection(std::size_t selected, std::size_t count,
                                    Navigation navigation) {
    if (count == 0) return 0;
    if (selected >= count) selected = 0;
    if (navigation == Navigation::Previous) return selected ? selected - 1 : count - 1;
    if (navigation == Navigation::Next) return selected + 1 == count ? 0 : selected + 1;
    return selected;
}

}  // namespace zectrix::app
