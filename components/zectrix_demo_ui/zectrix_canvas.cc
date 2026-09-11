#include "zectrix_canvas.h"

#include <algorithm>
#include <cstdlib>

#include "zectrix_ascii_font_8x16.h"
#include "zectrix_utf8.h"
#include "sdkconfig.h"
#if CONFIG_ZECTRIX_ENABLE_READER
#include "zectrix_reader.h"
#elif CONFIG_ZECTRIX_ENABLE_UI_CHINESE
#include "zectrix_ui_chinese_font.h"
#endif

namespace {
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

void PaintGlyph(ZectrixCanvas& canvas, int x, int y, const Glyph& glyph, int scale, bool inverted) {
    if (inverted) canvas.FillRect(x, y, glyph.width * scale, 16 * scale, true);
    for (int row = 0; row < 16; ++row) {
        const auto bits = glyph.Row(row);
        for (int col = 0; col < glyph.width; ++col) {
            const bool set = bits & (0x8000U >> col);
            if (set || inverted)
                canvas.FillRect(x + col * scale, y + row * scale, scale, scale, inverted ? !set : true);
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
                         bool inverted) {
    if (text == nullptr || scale <= 0) {
        return;
    }
    while (*text) {
        const auto glyph = UiGlyph(zectrix::ui::NextUtf8(text));
        PaintGlyph(*this, x, y, glyph, scale, inverted);
        x += glyph.width * scale;
    }
}

void ZectrixCanvas::TextCentered(int y, const char* text, int scale,
                                 bool inverted) {
    Text((kWidth - TextWidth(text, scale)) / 2, y, text, scale, inverted);
}

int ZectrixCanvas::TextWidth(const char* text, int scale) const {
    if (text == nullptr || scale <= 0) {
        return 0;
    }

    int width = 0;
    while (*text) width += UiGlyph(zectrix::ui::NextUtf8(text)).width * scale;
    return width;
}

void ZectrixCanvas::TextFitted(int x, int y, const char* text, int max_width, bool inverted) {
    if (!text || max_width <= 0) return;
    if (TextWidth(text) <= max_width) { Text(x, y, text, 1, inverted); return; }
    const int ellipsis = TextWidth("...");
    if (max_width < ellipsis) return;
    while (*text) {
        const auto glyph = UiGlyph(zectrix::ui::NextUtf8(text));
        if (glyph.width + ellipsis > max_width) break;
        PaintGlyph(*this, x, y, glyph, 1, inverted);
        x += glyph.width;
        max_width -= glyph.width;
    }
    Text(x, y, "...", 1, inverted);
}
