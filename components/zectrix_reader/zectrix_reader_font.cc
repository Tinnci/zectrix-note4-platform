#include "zectrix_reader.h"

extern "C" const uint8_t zectrix_reader_font_data[];

namespace zectrix::reader {
namespace {
struct Range { uint32_t first, last; };
constexpr Range kRanges[] = {
    {0x20, 0x2ff}, {0x2000, 0x206f}, {0x3000, 0x30ff}, {0x31f0, 0x31ff},
    {0x3400, 0x9fff}, {0xac00, 0xd7a3}, {0xff00, 0xffef}, {0xfffd, 0xfffd},
};
}  // namespace

const uint8_t* GlyphBitmap(uint32_t codepoint) {
    std::size_t index = 0;
    for (const auto& range : kRanges) {
        if (codepoint >= range.first && codepoint <= range.last)
            return zectrix_reader_font_data + 33 * (index + codepoint - range.first);
        index += range.last - range.first + 1;
    }
    return zectrix_reader_font_data + 33 * (index - 1);
}

int FontHeight(FontSize size) { return size == FontSize::Large ? 24 : 16; }
int GlyphWidth(uint32_t codepoint, FontSize size) {
    return GlyphBitmap(codepoint)[0] * FontHeight(size) / 16;
}
bool IsCjk(uint32_t cp) {
    return (cp >= 0x2e80 && cp <= 0x9fff) || (cp >= 0xac00 && cp <= 0xd7ff) ||
        (cp >= 0xf900 && cp <= 0xfaff) || (cp >= 0xff00 && cp <= 0xffef) ||
        (cp >= 0x20000 && cp <= 0x323af);
}
}  // namespace zectrix::reader
