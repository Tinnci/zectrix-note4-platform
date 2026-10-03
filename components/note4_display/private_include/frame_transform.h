#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace note4::display::detail {

// Pixel transforms only: no controller, power, waveform, SPI or allocation policy.
// Source and destination must not overlap. Callers validate size and geometry.
inline void RotateHalfTurn(const uint8_t* source, uint8_t* destination,
                           std::size_t size, bool gray) {
    for (std::size_t i = 0; i < size; ++i) {
        uint8_t value = source[size - i - 1];
        if (gray) value = static_cast<uint8_t>((value << 4) | (value >> 4));
        else {
            value = static_cast<uint8_t>(((value & 0x55) << 1) | ((value >> 1) & 0x55));
            value = static_cast<uint8_t>(((value & 0x33) << 2) | ((value >> 2) & 0x33));
            value = static_cast<uint8_t>((value << 4) | (value >> 4));
        }
        destination[i] = value;
    }
}

// Portrait rows are tightly packed: 300 pixels do not have byte-aligned scanlines.
inline void RotatePortraitMono(const uint8_t* source, uint8_t* destination,
                               int panel_width, int panel_height, bool flipped) {
    std::memset(destination, 0xff, static_cast<std::size_t>(panel_width) * panel_height / 8);
    for (int y = 0; y < panel_width; ++y) {
        for (int x = 0; x < panel_height; ++x) {
            const auto bit = static_cast<std::size_t>(y) * panel_height + x;
            if ((source[bit / 8] & (0x80 >> (bit & 7))) == 0) {
                const auto target = flipped ? static_cast<std::size_t>(panel_height - x - 1) * panel_width + y
                    : static_cast<std::size_t>(x) * panel_width + (panel_width - y - 1);
                destination[target / 8] &= static_cast<uint8_t>(~(0x80 >> (target & 7)));
            }
        }
    }
}

// Patches, unlike portrait frames, pad each row to the next byte; keep padding white.
inline void RotateMonoPatch(const uint8_t* source, uint8_t* destination, int width, int height) {
    const auto stride = static_cast<std::size_t>((width + 7) / 8);
    std::memset(destination, 0xff, stride * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if ((source[static_cast<std::size_t>(y) * stride + x / 8] & (0x80 >> (x & 7))) == 0) {
                const int dx = width - x - 1, dy = height - y - 1;
                destination[static_cast<std::size_t>(dy) * stride + dx / 8] &=
                    static_cast<uint8_t>(~(0x80 >> (dx & 7)));
            }
        }
    }
}

}  // namespace note4::display::detail
