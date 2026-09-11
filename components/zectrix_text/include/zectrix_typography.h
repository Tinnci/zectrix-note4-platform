#pragma once

#include "zectrix/sdk/text_style.h"

namespace zectrix::text {
using sdk::TextStyle;
using sdk::HasStyle;

constexpr bool IsCjk(uint32_t cp) {
    return (cp >= 0x2e80 && cp <= 0x9fff) || (cp >= 0xac00 && cp <= 0xd7ff) ||
        (cp >= 0xf900 && cp <= 0xfaff) || (cp >= 0xff00 && cp <= 0xffef) ||
        (cp >= 0x20000 && cp <= 0x323af);
}

struct GlyphMetrics {
    int width;
    int height;
    int advance;
    int weight;
    int slant;
    bool underline;
};

// All extents are device pixels. Keycap padding belongs to a complete run.
constexpr GlyphMetrics MeasureGlyph(uint32_t cp, int source_width, int height, TextStyle style) {
    const bool space = cp == ' ' || cp == 0xa0 || (cp >= 0x2000 && cp <= 0x200a) || cp == 0x3000;
    const bool dense = source_width >= 16 || IsCjk(cp);
    const int weight = !dense && !space && HasStyle(style, TextStyle::Bold) ? height / 16 : 0;
    const int slant = !dense && !space && HasStyle(style, TextStyle::Italic) ? height / 8 : 0;
    // Underlining preserves the counters and one-pixel gaps of CJK bitmaps.
    const bool underline = HasStyle(style, TextStyle::Underline) ||
        (dense && HasStyle(style, TextStyle::Bold | TextStyle::Italic));
    const int width = source_width * height / 16;
    return {width, height + (underline ? 2 : 0), width + weight + slant, weight, slant, underline};
}

constexpr bool InkAt(int x, int y, TextStyle style) {
    // Screen-anchored 75% Bayer ink is stable across clipped redraws.
    constexpr uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6},
                                    {3, 11, 1, 9}, {15, 7, 13, 5}};
    return !HasStyle(style, TextStyle::Dim) || bayer[y & 3][x & 3] < 12;
}
}  // namespace zectrix::text
