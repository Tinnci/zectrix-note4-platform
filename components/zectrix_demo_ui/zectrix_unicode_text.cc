#include "zectrix_unicode_text.h"
#include "zectrix_utf8.h"
#include "sdkconfig.h"
#if CONFIG_ZECTRIX_ENABLE_READER
#include "zectrix_reader.h"
#endif

namespace zectrix::ui {
#if CONFIG_ZECTRIX_ENABLE_READER
using namespace zectrix::reader;

void DrawGlyph(ZectrixCanvas& canvas, int x, int y, uint32_t cp, FontSize font, bool inverted) {
    const auto* bitmap = GlyphBitmap(cp);
    const int height = FontHeight(font);
    const int width = GlyphWidth(cp, font);
    for (int row = 0; row < height; ++row) {
        const int source_row = row * 16 / height;
        const uint16_t bits = static_cast<uint16_t>(bitmap[1 + source_row * 2]) << 8 |
                              bitmap[2 + source_row * 2];
        for (int col = 0; col < width; ++col)
            if (bits & (0x8000 >> (col * 16 / height))) canvas.Pixel(x + col, y + row, !inverted);
    }
}
#endif

void DrawUtf8Line(ZectrixCanvas& canvas, int x, int y, const char* text, int width, bool inverted) {
    if (!text || width <= 0) return;
#if CONFIG_ZECTRIX_ENABLE_READER
    const int right = x + width;
    int measured = 0;
    const char* probe = text;
    while (*probe) measured += GlyphWidth(NextUtf8(probe), FontSize::Small);
    const int ellipsis = measured > width ? GlyphWidth(0x2026, FontSize::Small) : 0;
    while (*text) {
        const auto cp = NextUtf8(text);
        const auto glyph_width = GlyphWidth(cp, FontSize::Small);
        if (x + glyph_width + ellipsis > right) {
            if (x + ellipsis <= right) DrawGlyph(canvas, x, y, 0x2026, FontSize::Small, inverted);
            break;
        }
        DrawGlyph(canvas, x, y, cp, FontSize::Small, inverted);
        x += glyph_width;
    }
#else
    canvas.TextFitted(x, y, text, width, inverted);
#endif
}
}  // namespace zectrix::ui
