#include "zectrix_unicode_text.h"

namespace zectrix::ui {
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

namespace {
uint32_t NextScalar(const char** text) {
    const auto first = static_cast<uint8_t>(*(*text)++);
    if (first < 0x80) return first;
    if (first < 0xc2 || first > 0xf4) return 0xfffd;
    const unsigned count = first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
    uint32_t cp = first & (count == 2 ? 31 : count == 3 ? 15 : 7);
    for (unsigned i = 1; i < count; ++i) {
        const auto byte = static_cast<uint8_t>(**text);
        if (byte < 0x80 || byte > 0xbf) return 0xfffd;
        ++*text;
        cp = (cp << 6) | (byte & 63);
    }
    return cp < (count == 2 ? 0x80U : count == 3 ? 0x800U : 0x10000U) ||
        cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) ? 0xfffd : cp;
}
}  // namespace

void DrawUtf8Line(ZectrixCanvas& canvas, int x, int y, const char* text, int width, bool inverted) {
    if (!text || width <= 0) return;
    const int right = x + width;
    while (*text) {
        const auto cp = NextScalar(&text);
        const auto glyph_width = GlyphWidth(cp, FontSize::Small);
        if (x + glyph_width + (*text ? 16 : 0) > right) {
            DrawGlyph(canvas, x, y, 0x2026, FontSize::Small, inverted);
            break;
        }
        DrawGlyph(canvas, x, y, cp, FontSize::Small, inverted);
        x += glyph_width;
    }
}
}  // namespace zectrix::ui
