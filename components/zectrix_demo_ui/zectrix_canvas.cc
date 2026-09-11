#include "zectrix_canvas.h"

#include <algorithm>
#include <climits>
#include <cstdlib>

#include "zectrix_ascii_font_8x16.h"
#include "zectrix_styled_glyph.h"
#include "zectrix_utf8.h"
#include "sdkconfig.h"
#if CONFIG_ZECTRIX_ENABLE_READER
#include "zectrix_reader.h"
#elif CONFIG_ZECTRIX_ENABLE_UI_CHINESE
#include "zectrix_ui_chinese_font.h"
#endif

namespace {
using zectrix::sdk::TextStyle;
using zectrix::sdk::HasStyle;
using zectrix::text::MeasureGlyph;
using zectrix::ui::detail::FillClipped;
using zectrix::ui::detail::PaintGlyph;

struct Glyph {
    int width;
    const uint16_t* ascii = nullptr;
#if CONFIG_ZECTRIX_ENABLE_READER
    zectrix::reader::BitmapGlyph unicode{};
#else
    const uint8_t* unicode = nullptr;
#endif
    uint16_t Row(int row) const {
#if CONFIG_ZECTRIX_ENABLE_READER
        return ascii ? ascii[row] : unicode.Row(row);
#else
        return ascii ? ascii[row] : static_cast<uint16_t>(unicode[1 + row * 2]) << 8 |
                                     unicode[2 + row * 2];
#endif
    }
};

Glyph UiGlyph(uint32_t cp) {
    if (cp >= 32 && cp < 127)
        return {kZectrixAsciiFontWidths[cp - 32], kZectrixAsciiFont8x16[cp - 32], {}};
#if CONFIG_ZECTRIX_ENABLE_READER
    const auto bitmap = zectrix::reader::GlyphBitmap(cp);
    return {bitmap.width, nullptr, bitmap};
#elif CONFIG_ZECTRIX_ENABLE_UI_CHINESE
    const auto* first = std::begin(kUiChineseCodepoints);
    const auto* last = std::end(kUiChineseCodepoints);
    const auto* found = std::lower_bound(first, last, cp);
    if (found != last && *found == cp) {
        const auto* bitmap = kUiChineseBitmaps[found - first];
        return {bitmap[0], nullptr, bitmap};
    }
#endif
    return {kZectrixAsciiFontWidths['?' - 32], kZectrixAsciiFont8x16['?' - 32], {}};
}

struct RunMetrics { int width = 0, height = 0; };

RunMetrics MeasureRun(const char* text, const char* end, const char* suffix,
                      int scale, TextStyle style) {
    RunMetrics result;
    if (!text || scale < 1 || scale > 16) return result;
    for (const char* span : {text, suffix}) {
        for (const char* p = span; p && *p && (span != text || p != end);) {
            const auto cp = zectrix::ui::NextUtf8(p);
            const auto glyph = MeasureGlyph(cp, UiGlyph(cp).width, 16 * scale, style);
            result.width += std::min(INT_MAX - result.width, glyph.advance);
            result.height = std::max(result.height, glyph.height);
        }
    }
    if (result.height && HasStyle(style, TextStyle::Keycap)) {
        result.width += std::min(INT_MAX - result.width, 6 * scale);
        result.height += 4 * scale;
    }
    return result;
}

void PaintRun(ZectrixCanvas& canvas, int x, int y, const char* text, const char* end,
              const char* suffix, int scale, bool inverted, TextStyle style) {
    const auto metrics = MeasureRun(text, end, suffix, scale, style);
    if (!metrics.height) return;
    if (inverted) FillClipped(canvas, x, y, metrics.width, metrics.height, true);
    int64_t cursor = x, top = y;
    if (HasStyle(style, TextStyle::Keycap)) {
        FillClipped(canvas, x, y, metrics.width, 1, !inverted);
        FillClipped(canvas, x, static_cast<int64_t>(y) + metrics.height - 1, metrics.width, 1, !inverted);
        FillClipped(canvas, x, y, 1, metrics.height, !inverted);
        FillClipped(canvas, static_cast<int64_t>(x) + metrics.width - 1, y, 1, metrics.height, !inverted);
        cursor += 3 * scale;
        top += 2 * scale;
    }
    for (const char* span : {text, suffix}) {
        for (const char* p = span; p && *p && (span != text || p != end);) {
            const auto cp = zectrix::ui::NextUtf8(p);
            const auto glyph = UiGlyph(cp);
            PaintGlyph(canvas, cursor, top, cp, glyph, 16 * scale, style, inverted);
            cursor += MeasureGlyph(cp, glyph.width, 16 * scale, style).advance;
            if (cursor >= canvas.clip().x + canvas.clip().width) break;
        }
    }
}
}  // namespace

void ZectrixCanvas::Clear(bool white) {
    if (clip_.x == 0 && clip_.y == 0 && clip_.width == kWidth &&
        clip_.height == kHeight) {
        pixels_.fill(white ? 0xff : 0x00);
    } else {
        FillRect(clip_.x, clip_.y, clip_.width, clip_.height, !white);
    }
}

void ZectrixCanvas::SetClip(Clip clip) {
    const int left = std::clamp(clip.x, 0, kWidth);
    const int top = std::clamp(clip.y, 0, kHeight);
    const int right = static_cast<int>(std::clamp<int64_t>(
        static_cast<int64_t>(clip.x) + std::max(0, clip.width), left, kWidth));
    const int bottom = static_cast<int>(std::clamp<int64_t>(
        static_cast<int64_t>(clip.y) + std::max(0, clip.height), top, kHeight));
    clip_ = {left, top, right - left, bottom - top};
}

void ZectrixCanvas::Pixel(int x, int y, bool black) {
    if (x < clip_.x || x >= clip_.x + clip_.width ||
        y < clip_.y || y >= clip_.y + clip_.height) {
        return;
    }
    uint8_t& byte = pixels_[static_cast<size_t>(y) * kStride + x / 8];
    const uint8_t mask = static_cast<uint8_t>(1U << (7 - (x & 7)));
    if (black) {
        byte &= static_cast<uint8_t>(~mask);
    } else {
        byte |= mask;
    }
}

void ZectrixCanvas::FillRect(int x, int y, int width, int height,
                             bool black) {
    const int left = std::max(0, x);
    const int top = std::max(0, y);
    const int right = std::min(kWidth, x + width);
    const int bottom = std::min(kHeight, y + height);
    for (int py = top; py < bottom; ++py) {
        for (int px = left; px < right; ++px) {
            Pixel(px, py, black);
        }
    }
}

void ZectrixCanvas::Rect(int x, int y, int width, int height, bool black) {
    Line(x, y, x + width - 1, y, black);
    Line(x, y + height - 1, x + width - 1, y + height - 1, black);
    Line(x, y, x, y + height - 1, black);
    Line(x + width - 1, y, x + width - 1, y + height - 1, black);
}

void ZectrixCanvas::Line(int x0, int y0, int x1, int y1, bool black) {
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        Pixel(x0, y0, black);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int twice = error * 2;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void ZectrixCanvas::Text(int x, int y, const char* text, int scale,
                         bool inverted, TextStyle style) {
    PaintRun(*this, x, y, text, nullptr, nullptr, scale, inverted, style);
}

void ZectrixCanvas::TextCentered(int y, const char* text, int scale,
                                 bool inverted, TextStyle style) {
    Text((kWidth - TextWidth(text, scale, style)) / 2, y, text, scale, inverted, style);
}

int ZectrixCanvas::TextWidth(const char* text, int scale, TextStyle style) const {
    return MeasureRun(text, nullptr, nullptr, scale, style).width;
}
int ZectrixCanvas::TextHeight(const char* text, int scale, TextStyle style) const {
    return MeasureRun(text, nullptr, nullptr, scale, style).height;
}

void ZectrixCanvas::TextFitted(int x, int y, const char* text, int max_width, bool inverted, TextStyle style) {
    if (!text || max_width <= 0) return;
    if (TextWidth(text, 1, style) <= max_width) { Text(x, y, text, 1, inverted, style); return; }
    const int ellipsis = TextWidth("...", 1, style);
    if (max_width < ellipsis) return;
    const char* end = text;
    int used = 0;
    while (*end) {
        const char* next = end;
        const auto cp = zectrix::ui::NextUtf8(next);
        const auto advance = MeasureGlyph(cp, UiGlyph(cp).width, 16, style).advance;
        if (advance > max_width - used - ellipsis) break;
        used += advance;
        end = next;
    }
    PaintRun(*this, x, y, text, end, "...", 1, inverted, style);
}
