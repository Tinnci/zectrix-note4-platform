#include "unicode_text.h"
#include "utf8.h"
#include "styled_glyph.h"
#include <climits>
#include "sdkconfig.h"
#if CONFIG_NOTE4_ENABLE_READER
#include "note4_reader.h"
#endif

namespace note4::ui {
#if CONFIG_NOTE4_ENABLE_READER
using namespace note4::reader;

void DrawGlyph(Canvas& canvas, int x, int y, uint32_t cp, FontSize font,
               bool inverted, sdk::TextStyle style) {
    detail::PaintGlyph(canvas, x, y, cp, GlyphBitmap(cp), FontHeight(font), style, inverted);
#if defined(NOTE4_FONT_TRACE) && NOTE4_FONT_TRACE
    const auto bitmap = GlyphBitmap(cp);
    uint16_t source_rows[16];
    for (int row = 0; row < 16; ++row) source_rows[row] = bitmap.Row(row);
    font_trace::Glyph(canvas, cp, 0, FontHeight(font) == 16 ? "Reader16" : "Reader24", "reader",
        FontHeight(font), x, y, GlyphWidth(cp, font, style),
        text::MeasureGlyph(cp, bitmap.width, FontHeight(font), style).height,
        inverted, source_rows, bitmap.width, static_cast<unsigned>(style));
#endif
}
#endif

void DrawUtf8Line(Canvas& canvas, int x, int y, const char* text, int width,
                  bool inverted, sdk::TextStyle style) {
    if (!text || width <= 0) return;
    if (sdk::HasStyle(style, sdk::TextStyle::Keycap)) {
        canvas.TextFitted(x, y, text, width, inverted, style);
        return;
    }
#if CONFIG_NOTE4_ENABLE_READER
    const int64_t right = static_cast<int64_t>(x) + width;
    int64_t cursor = x;
    int measured = 0;
    const char* probe = text;
    while (*probe) measured += std::min(INT_MAX - measured, GlyphWidth(NextUtf8(probe), FontSize::Small, style));
    const int ellipsis = measured > width ? GlyphWidth(0x2026, FontSize::Small, style) : 0;
    while (*text) {
        const auto cp = NextUtf8(text);
        const auto glyph_width = GlyphWidth(cp, FontSize::Small, style);
        if (cursor + glyph_width + ellipsis > right) {
            if (cursor + ellipsis <= right && cursor <= INT_MAX)
                DrawGlyph(canvas, static_cast<int>(cursor), y, 0x2026, FontSize::Small, inverted, style);
            break;
        }
        if (cursor > INT_MAX) break;
        DrawGlyph(canvas, static_cast<int>(cursor), y, cp, FontSize::Small, inverted, style);
        cursor += glyph_width;
    }
#else
    canvas.TextFitted(x, y, text, width, inverted, style);
#endif
}
}  // namespace note4::ui
