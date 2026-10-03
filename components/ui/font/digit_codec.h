#pragma once
#include <cstddef>
#include <cstdint>

// Raw MSB-first / row runs / row-XOR runs / column runs. Six-byte XOR row state.
// A malformed stream may emit a prefix before returning false; static firmware
// assets are validated offline and tests verify exact decoding of every glyph.
template <bool EnableXor = true, bool EnableColumn = true, typename Emit>
bool ZectrixDecodeDigit(const uint8_t* data, std::size_t size, unsigned width,
                       unsigned height, uint8_t codec, Emit emit) {
    if (!data || width == 0 || width > 42 || height == 0 || height > 128 || codec > 3 ||
        (!EnableXor && codec == 2) || (!EnableColumn && codec == 3)) return false;
    const unsigned count = width * height;
    if (codec == 0 && size != (count + 7) / 8) return false;
    uint8_t previous[6] = {};
    std::size_t cursor = 0;
    unsigned remaining = 0, position = 0;
    uint8_t run = 0;
    const bool column_major = EnableColumn && codec == 3;
    const unsigned outer = column_major ? width : height, inner = column_major ? height : width;
    for (unsigned a = 0; a < outer; ++a) {
        for (unsigned b = 0; b < inner; ++b, ++position) {
            const unsigned col = column_major ? a : b, row = column_major ? b : a;
            bool black;
            if (codec == 0) {
                black = (data[position / 8] & (128 >> (position % 8))) != 0;
            } else {
                if (remaining == 0) {
                    if (cursor == size) return false;
                    run = data[cursor++];
                    remaining = (run & 127) + 1;
                }
                black = (run & 128) != 0;
                --remaining;
            }
            if constexpr (EnableXor) { if (codec == 2) {
                const uint8_t mask = static_cast<uint8_t>(128 >> (col % 8));
                black = black != ((previous[col / 8] & mask) != 0);
                if (black) previous[col / 8] |= mask;
                else previous[col / 8] &= static_cast<uint8_t>(~mask);
            } }
            emit(col, row, black);
        }
    }
    return codec == 0 || (remaining == 0 && cursor == size);
}
