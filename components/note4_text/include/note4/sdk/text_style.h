#pragma once

#include <cstdint>

namespace note4::sdk {
inline namespace v2 {

// Styles describe intent; the renderer may adapt emphasis to dense glyphs.
enum class TextStyle : std::uint8_t {
    Regular = 0,
    Bold = 1,
    Italic = 2,
    Dim = 4,
    Underline = 8,
    Keycap = 16,
};

constexpr TextStyle operator|(TextStyle left, TextStyle right) {
    return static_cast<TextStyle>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
}
constexpr TextStyle operator&(TextStyle left, TextStyle right) {
    return static_cast<TextStyle>(static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right));
}
constexpr bool HasStyle(TextStyle value, TextStyle flags) {
    return (value & flags) != TextStyle::Regular;
}

}  // namespace v2
}  // namespace note4::sdk
