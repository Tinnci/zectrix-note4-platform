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
