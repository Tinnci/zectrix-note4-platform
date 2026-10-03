#pragma once
#include <cstdint>

namespace note4::ui {
enum class DigitStyle : uint8_t { Serif, Round, Ink, Dots, Deco };
inline constexpr unsigned kDigitStyleCount = 5;
inline constexpr char kDigitStyleSettingKey[] = "ui.digit_font";
constexpr DigitStyle NormalizeDigitStyle(uint32_t value) {
    return value < kDigitStyleCount ? static_cast<DigitStyle>(value) : DigitStyle::Serif;
}
constexpr const char* DigitStyleName(DigitStyle style) {
    constexpr const char* names[] = {"A Serif", "B Round", "C Ink", "D Dots", "E Deco"};
    return names[static_cast<unsigned>(NormalizeDigitStyle(static_cast<unsigned>(style)))];
}
}  // namespace note4::ui
