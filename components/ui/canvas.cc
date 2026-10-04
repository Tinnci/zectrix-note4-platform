#include "canvas.h"

#include <algorithm>
#include <climits>
#include <cstdlib>

#include "ascii_font_8x16.h"
#include "large_digits.h"
#include "digit_codec.h"
#include "editorial_font.h"
#include "styled_glyph.h"
#include "utf8.h"
#include "sdkconfig.h"
#if CONFIG_NOTE4_ENABLE_READER
#include "note4_reader.h"
#elif CONFIG_NOTE4_ENABLE_UI_CHINESE
#include "ui_chinese_font.h"
#endif

namespace {
using note4::sdk::TextStyle;
using note4::sdk::HasStyle;
using note4::text::MeasureGlyph;
using note4::ui::detail::FillClipped;
using note4::ui::detail::PaintGlyph;

struct Glyph {
    int width;
    const uint16_t* ascii = nullptr;
#if CONFIG_NOTE4_ENABLE_READER
    note4::reader::BitmapGlyph unicode{};
#else
    const uint8_t* unicode = nullptr;
#endif
    uint16_t Row(int row) const {
#if CONFIG_NOTE4_ENABLE_READER
        return ascii ? ascii[row] : unicode.Row(row);
#else
        return ascii ? ascii[row] : static_cast<uint16_t>(unicode[1 + row * 2]) << 8 |
                                     unicode[2 + row * 2];
#endif
    }
};

Glyph UiGlyph(uint32_t cp) {
    if (cp >= 32 && cp < 127)
        return {kNote4AsciiFontWidths[cp - 32], kNote4AsciiFont8x16[cp - 32], {}};
#if CONFIG_NOTE4_ENABLE_READER
    const auto bitmap = note4::reader::GlyphBitmap(cp);
    return {bitmap.width, nullptr, bitmap};
#elif CONFIG_NOTE4_ENABLE_UI_CHINESE
    const auto* first = std::begin(kUiChineseCodepoints);
    const auto* last = std::end(kUiChineseCodepoints);
    const auto* found = std::lower_bound(first, last, cp);
    if (found != last && *found == cp) {
        const auto* bitmap = kUiChineseBitmaps[found - first];
        return {bitmap[0], nullptr, bitmap};
    }
#endif
    return {kNote4AsciiFontWidths['?' - 32], kNote4AsciiFont8x16['?' - 32], {}};
}

struct RunMetrics { int width = 0, height = 0; };

RunMetrics MeasureRun(const char* text, const char* end, const char* suffix,
                      int scale, TextStyle style) {
    RunMetrics result;
    if (!text || scale < 1 || scale > 16) return result;
    for (const char* span : {text, suffix}) {
        for (const char* p = span; p && *p && (span != text || p != end);) {
            const auto cp = note4::ui::NextUtf8(p);
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

void PaintRun(Canvas& canvas, int x, int y, const char* text, const char* end,
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
            const auto cp = note4::ui::NextUtf8(p);
            const auto glyph = UiGlyph(cp);
            PaintGlyph(canvas, cursor, top, cp, glyph, 16 * scale, style, inverted);
            cursor += MeasureGlyph(cp, glyph.width, 16 * scale, style).advance;
            if (cursor >= canvas.clip().x + canvas.clip().width) break;
        }
    }
}
}  // namespace

namespace {
struct EditorialFace {
    const EditorialGlyph* first;
    const EditorialGlyph* last;
    const uint8_t* data;
    int height;
    int size;
};
EditorialFace Face(Canvas::UiFace face) {
    switch (face) {
        case Canvas::UiFace::Caption: return {std::begin(kEditorialCaptionGlyphs), std::end(kEditorialCaptionGlyphs), kEditorialCaptionData, kEditorialCaptionHeight, kEditorialCaptionSize};
        case Canvas::UiFace::Navigation: return {std::begin(kEditorialNavigationGlyphs), std::end(kEditorialNavigationGlyphs), kEditorialNavigationData, kEditorialNavigationHeight, kEditorialNavigationSize};
        case Canvas::UiFace::Selected: return {std::begin(kEditorialSelectedGlyphs), std::end(kEditorialSelectedGlyphs), kEditorialSelectedData, kEditorialSelectedHeight, kEditorialSelectedSize};
        case Canvas::UiFace::Heading: return {std::begin(kEditorialHeadingGlyphs), std::end(kEditorialHeadingGlyphs), kEditorialHeadingData, kEditorialHeadingHeight, kEditorialHeadingSize};
        case Canvas::UiFace::Compact: return {std::begin(kEditorialCompactGlyphs), std::end(kEditorialCompactGlyphs), kEditorialCompactData, kEditorialCompactHeight, kEditorialCompactSize};
        case Canvas::UiFace::Label: return {std::begin(kEditorialLabelGlyphs), std::end(kEditorialLabelGlyphs), kEditorialLabelData, kEditorialLabelHeight, kEditorialLabelSize};
        case Canvas::UiFace::Micro: return {std::begin(kEditorialMicroGlyphs), std::end(kEditorialMicroGlyphs), kEditorialMicroData, kEditorialMicroHeight, kEditorialMicroSize};
    }
    return Face(Canvas::UiFace::Navigation);
}
const EditorialGlyph* FindGlyph(const EditorialFace& face, uint32_t cp) {
    const auto* found = std::lower_bound(face.first, face.last, cp,
        [](const EditorialGlyph& glyph, uint32_t value) { return glyph.codepoint < value; });
    return found != face.last && found->codepoint == cp ? found : nullptr;
}
int Advance(const EditorialFace& face, uint32_t cp) {
    const auto* glyph = FindGlyph(face, cp);
    return glyph ? glyph->width : UiGlyph(cp).width * face.size / 16;
}
}

int Canvas::UiTextHeight(UiFace face) { return Face(face).height; }
int Canvas::UiTextWidth(const char* text, UiFace name) const {
    const auto face = Face(name);
    int width = 0;
    for (const char* p = text; p && *p;) {
        const int advance = Advance(face, note4::ui::NextUtf8(p));
        width += std::min(INT_MAX - width, advance);
    }
    return width;
}
void Canvas::UiText(int x, int y, const char* text, int max_width, UiFace name, bool inverted) {
    if (!text || max_width <= 0) return;
    const auto face = Face(name);
    const bool truncate = UiTextWidth(text, name) > max_width;
    const int ellipsis = truncate ? Advance(face, 0x2026) : 0;
    if (ellipsis > max_width) return;
    int64_t cursor = x;
    const int64_t right = int64_t(x) + max_width;
    const auto paint = [&](uint32_t cp) {
        if (cursor > INT_MAX || cursor < INT_MIN) return;
        const auto* glyph = FindGlyph(face, cp);
        if (glyph) {
            if (inverted) FillClipped(*this, cursor, y, glyph->width, face.height, true);
            for (int row = 0; row < glyph->rows; ++row) for (int col = 0; col < glyph->width; ++col) {
                const unsigned bit = row * glyph->width + col;
                if (face.data[glyph->offset + bit / 8] & (0x80 >> (bit & 7)))
                    FillClipped(*this, cursor + col, int64_t(y) + glyph->top + row, 1, 1, !inverted);
            }
        } else {
            // Arbitrary document names retain Unicode coverage without a full CJK font.
            PaintGlyph(*this, cursor, int64_t(y) + 2, cp, UiGlyph(cp), face.size, TextStyle::Regular, inverted);
        }
    };
    for (const char* p = text; *p;) {
        const auto cp = note4::ui::NextUtf8(p);
        const int advance = Advance(face, cp);
        if (cursor + advance + ellipsis > right) { paint(0x2026); return; }
        paint(cp);
        cursor += advance;
    }
}

void Canvas::Clear(bool white) {
    if (clip_.x == 0 && clip_.y == 0 && clip_.width == width() &&
        clip_.height == height()) {
        pixels_.fill(white ? 0xff : 0x00);
    } else {
        FillRect(clip_.x, clip_.y, clip_.width, clip_.height, !white);
    }
}

void Canvas::SetClip(Clip clip) {
    const int left = std::clamp(clip.x, 0, width());
    const int top = std::clamp(clip.y, 0, height());
    const int right = static_cast<int>(std::clamp<int64_t>(
        static_cast<int64_t>(clip.x) + std::max(0, clip.width), left, width()));
    const int bottom = static_cast<int>(std::clamp<int64_t>(
        static_cast<int64_t>(clip.y) + std::max(0, clip.height), top, height()));
    clip_ = {left, top, right - left, bottom - top};
}

void Canvas::Pixel(int x, int y, bool black) {
    if (x < clip_.x || x >= clip_.x + clip_.width ||
        y < clip_.y || y >= clip_.y + clip_.height) {
        return;
    }
    const auto bit = static_cast<size_t>(y) * width() + x;
    uint8_t& byte = pixels_[bit / 8];
    const uint8_t mask = static_cast<uint8_t>(1U << (7 - (bit & 7)));
    if (black) {
        byte &= static_cast<uint8_t>(~mask);
    } else {
        byte |= mask;
    }
}

void Canvas::FillRect(int x, int y, int width, int height,
                             bool black) {
    const int left = std::max(0, x);
    const int top = std::max(0, y);
    const int right = std::min(this->width(), x + width);
    const int bottom = std::min(this->height(), y + height);
    for (int py = top; py < bottom; ++py) {
        for (int px = left; px < right; ++px) {
            Pixel(px, py, black);
        }
    }
}

void Canvas::Rect(int x, int y, int width, int height, bool black) {
    Line(x, y, x + width - 1, y, black);
    Line(x, y + height - 1, x + width - 1, y + height - 1, black);
    Line(x, y, x, y + height - 1, black);
    Line(x + width - 1, y, x + width - 1, y + height - 1, black);
}

void Canvas::Line(int x0, int y0, int x1, int y1, bool black) {
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

void Canvas::Text(int x, int y, const char* text, int scale,
                         bool inverted, TextStyle style) {
    PaintRun(*this, x, y, text, nullptr, nullptr, scale, inverted, style);
}

void Canvas::TextCentered(int y, const char* text, int scale,
                                 bool inverted, TextStyle style) {
    Text((width() - TextWidth(text, scale, style)) / 2, y, text, scale, inverted, style);
}

int Canvas::LargeNumberWidth(unsigned value) const {
    if (value > 99) return 0;
    const auto base = static_cast<unsigned>(digit_style_) * 10;
    return kNote4DigitGlyphs[base + value / 10].width + 3 + kNote4DigitGlyphs[base + value % 10].width;
}

void Canvas::LargeNumber(int x, int y, unsigned value, bool inverted) {
    const int width = LargeNumberWidth(value);
    if (value > 99 || x >= clip_.x + clip_.width || y >= clip_.y + clip_.height ||
        static_cast<int64_t>(x) + width <= clip_.x ||
        static_cast<int64_t>(y) + kLargeNumberHeight <= clip_.y) return;
    if (inverted) FillRect(x, y, width, kLargeNumberHeight, true);
    const unsigned digits[] = {value / 10, value % 10};
    int origin = x;
    for (const auto digit : digits) {
        const auto& glyph = kNote4DigitGlyphs[static_cast<unsigned>(digit_style_) * 10 + digit];
        const auto* data = kNote4DigitData + glyph.offset;
        Note4DecodeDigit<kNote4DigitUsesXor, kNote4DigitUsesColumn>(data, glyph.size, glyph.width, kLargeNumberHeight, glyph.codec,
            [&](unsigned col, unsigned row, bool black) {
                if (black) Pixel(origin + static_cast<int>(col), y + static_cast<int>(row), !inverted);
            });
        origin += glyph.width + 3;
    }
}

int Canvas::TextWidth(const char* text, int scale, TextStyle style) const {
    return MeasureRun(text, nullptr, nullptr, scale, style).width;
}
int Canvas::TextHeight(const char* text, int scale, TextStyle style) const {
    return MeasureRun(text, nullptr, nullptr, scale, style).height;
}

void Canvas::TextFitted(int x, int y, const char* text, int max_width, bool inverted, TextStyle style) {
    if (!text || max_width <= 0) return;
    if (TextWidth(text, 1, style) <= max_width) { Text(x, y, text, 1, inverted, style); return; }
    const int ellipsis = TextWidth("...", 1, style);
    if (max_width < ellipsis) return;
    const char* end = text;
    int used = 0;
    while (*end) {
        const char* next = end;
        const auto cp = note4::ui::NextUtf8(next);
        const auto advance = MeasureGlyph(cp, UiGlyph(cp).width, 16, style).advance;
        if (advance > max_width - used - ellipsis) break;
        used += advance;
        end = next;
    }
    PaintRun(*this, x, y, text, end, "...", 1, inverted, style);
}
