#include "zectrix_reader.h"
#include "zectrix_typography.h"

extern "C" const uint8_t zectrix_reader_font_data[];

namespace zectrix::reader {
namespace {
struct Range { uint32_t first, last; };
constexpr Range kRanges[] = {
    {0x20, 0x2ff}, {0x2000, 0x206f}, {0x3000, 0x30ff}, {0x31f0, 0x31ff},
    {0x3400, 0x9fff}, {0xac00, 0xd7a3}, {0xff00, 0xffef}, {0xfffd, 0xfffd},
};
constexpr std::size_t GlyphCount() {
    std::size_t count = 0;
    for (const auto& range : kRanges) count += range.last - range.first + 1;
    return count;
}
constexpr auto kGlyphCount = GlyphCount();
constexpr auto kWidthBytes = (kGlyphCount + 7) / 8;
constexpr auto kTileOffset = kWidthBytes + kGlyphCount * 8;

std::size_t GlyphIndex(uint32_t codepoint) {
    std::size_t index = 0;
    for (const auto& range : kRanges) {
        if (codepoint >= range.first && codepoint <= range.last)
            return index + codepoint - range.first;
        index += range.last - range.first + 1;
    }
    return index - 1;
}

uint8_t Width(std::size_t index) {
    return zectrix_reader_font_data[index / 8] & (1 << (index % 8)) ? 16 : 8;
}
}  // namespace

BitmapGlyph GlyphBitmap(uint32_t codepoint) {
    const auto index = GlyphIndex(codepoint);
    const auto* ids = zectrix_reader_font_data + kWidthBytes + index * 8;
    BitmapGlyph glyph;
    glyph.width = Width(index);
    for (std::size_t tile = 0; tile < glyph.tiles.size(); ++tile) {
        const auto id = static_cast<uint16_t>(ids[tile * 2]) | static_cast<uint16_t>(ids[tile * 2 + 1]) << 8;
        glyph.tiles[tile] = zectrix_reader_font_data + kTileOffset + id * 8;
    }
    return glyph;
}

int FontHeight(FontSize size) { return size == FontSize::Large ? 24 : 16; }
int GlyphWidth(uint32_t codepoint, FontSize size, TextStyle style) {
    return text::MeasureGlyph(codepoint, Width(GlyphIndex(codepoint)), FontHeight(size), style).advance;
}
int GlyphHeight(uint32_t codepoint, FontSize size, TextStyle style) {
    return text::MeasureGlyph(codepoint, Width(GlyphIndex(codepoint)), FontHeight(size), style).height;
}
bool IsCjk(uint32_t cp) { return text::IsCjk(cp); }
}  // namespace zectrix::reader
