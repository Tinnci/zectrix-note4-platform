#pragma once

#include <cstdint>

namespace note4::sdk {
inline namespace v2 {

enum class Button : std::uint8_t { Up, Down, Ok };
enum class InputAction : std::uint8_t { Click, LongPress };

struct InputEvent {
    Button button = Button::Ok;
    InputAction action = InputAction::Click;
};

}  // namespace v2
}  // namespace note4::sdk
