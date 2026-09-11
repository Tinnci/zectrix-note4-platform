#pragma once

#include "zectrix_canvas.h"
#include "zectrix_typography.h"

#include <algorithm>

namespace zectrix::ui::detail {

inline void FillClipped(ZectrixCanvas& canvas, int64_t x, int64_t y,
                        int width, int height, bool black) {
    const auto clip = canvas.clip();
    const auto left = std::max<int64_t>(x, clip.x), top = std::max<int64_t>(y, clip.y);
    const auto right = std::min<int64_t>(x + width, clip.x + clip.width);
    const auto bottom = std::min<int64_t>(y + height, clip.y + clip.height);
    if (right > left && bottom > top)
        canvas.FillRect(static_cast<int>(left), static_cast<int>(top),
                        static_cast<int>(right - left), static_cast<int>(bottom - top), black);
}

// Both UI and reader glyphs provide immutable 16-row bitmaps. Sampling and
// expansion share the exact extents used by fitting and page layout.
template<class Glyph>
void PaintGlyph(ZectrixCanvas& canvas, int64_t x, int64_t y, uint32_t cp,
                const Glyph& glyph, int height, sdk::TextStyle style, bool inverted,
                bool opaque = false) {
    const auto metrics = text::MeasureGlyph(cp, glyph.width, height, style);
    const auto clip = canvas.clip();
    const int first_row = static_cast<int>(std::clamp<int64_t>(clip.y - y, 0, metrics.height));
    const int last_row = static_cast<int>(std::clamp<int64_t>(clip.y + clip.height - y, 0, metrics.height));
    const int first_col = static_cast<int>(std::clamp<int64_t>(clip.x - x, 0, metrics.advance));
    const int last_col = static_cast<int>(std::clamp<int64_t>(clip.x + clip.width - x, 0, metrics.advance));
    for (int row = first_row; row < last_row; ++row) {
        const uint16_t bits = row < height ? glyph.Row(row * 16 / height) : 0;
        const int shift = row < height ? metrics.slant * (height - 1 - row) / (height - 1) : 0;
        for (int col = first_col; col < last_col; ++col) {
            bool ink = metrics.underline && row == height + 1;
            for (int delta = 0; !ink && bits && delta <= metrics.weight; ++delta) {
                const int source = col - shift - delta;
                if (source >= 0 && source < metrics.width)
                    ink = bits & (0x8000U >> (source * 16 / height));
            }
            const int px = static_cast<int>(x + col), py = static_cast<int>(y + row);
            ink = ink && text::InkAt(px, py, style);
            if (ink || opaque) canvas.Pixel(px, py, ink != inverted);
        }
    }
}
}  // namespace zectrix::ui::detail
