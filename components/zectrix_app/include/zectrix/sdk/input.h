#pragma once

#include <cstdint>

namespace zectrix::sdk {
inline namespace v1 {

enum class Button : std::uint8_t { Up, Down, Ok };
enum class InputAction : std::uint8_t { Click, LongPress };

struct InputEvent {
    Button button = Button::Ok;
    InputAction action = InputAction::Click;
};

}  // namespace v1
}  // namespace zectrix::sdk
