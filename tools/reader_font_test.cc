#include "zectrix_reader.h"

#include <cassert>
#include <chrono>
#include <cstdio>

extern "C" const uint8_t zectrix_reader_font_data[], zectrix_reader_font_end[];

int main(int argc, char** argv) {
    using namespace zectrix::reader;
    FILE* reference = argc > 1 ? std::fopen(argv[1], "rb") : nullptr;
    assert(argc == 1 || reference);
    constexpr uint32_t ranges[][2] = {
        {0x20, 0x2FF}, {0x2000, 0x206F}, {0x3000, 0x30FF}, {0x31F0, 0x31FF},
        {0x3400, 0x9FFF}, {0xAC00, 0xD7A3}, {0xFF00, 0xFFEF}, {0xFFFD, 0xFFFD},
    };
    const auto begin = reinterpret_cast<uintptr_t>(zectrix_reader_font_data);
    const auto end = reinterpret_cast<uintptr_t>(zectrix_reader_font_end);
    std::size_t count = 0;
    for (const auto& range : ranges) {
        for (uint32_t cp = range[0]; cp <= range[1]; ++cp) {
            const auto glyph = GlyphBitmap(cp);
            assert(glyph.width == 8 || glyph.width == 16);
            assert(GlyphWidth(cp, FontSize::Small) == glyph.width);
            assert(GlyphWidth(cp, FontSize::Large) == glyph.width * 3 / 2);
            for (const auto* tile : glyph.tiles) {
                const auto address = reinterpret_cast<uintptr_t>(tile);
                assert(address >= begin && address + 8 <= end);
            }
            const auto retained = glyph.Row(count % 16);
            GlyphBitmap(cp + 1);
            assert(glyph.Row(count % 16) == retained);
            assert(glyph.Row(16) == 0 && glyph.Row(~0U) == 0);
            if (reference) {
                uint8_t raw[33];
                assert(std::fread(raw, 1, sizeof(raw), reference) == sizeof(raw));
                assert(glyph.width == raw[0]);
                for (unsigned row = 0; row < 16; ++row)
                    assert(glyph.Row(row) == (static_cast<uint16_t>(raw[1 + row * 2]) << 8 | raw[2 + row * 2]));
            }
            ++count;
        }
    }
    if (reference) {
        assert(std::fgetc(reference) == EOF && !std::ferror(reference));
        std::fclose(reference);
    }
    const auto replacement = GlyphBitmap(0xFFFD);
    for (uint32_t cp : {0U, 0xD800U, 0x20000U, 0x10FFFFU, 0xFFFFFFFFU}) {
        const auto glyph = GlyphBitmap(cp);
        assert(glyph.width == replacement.width && glyph.tiles == replacement.tiles);
    }
    assert(GlyphBitmap('A').width == 8 && GlyphBitmap(U'中').width == 16);
    assert(BitmapGlyph{}.Row(0) == 0);

    // Report a random-access page workload, not an EPD latency claim or limit.
    volatile uint32_t pixels = 0;
    const auto start = std::chrono::steady_clock::now();
    constexpr unsigned repeats = 100;
    for (unsigned repeat = 0; repeat < repeats; ++repeat) {
        for (unsigned slot = 0; slot < Page::kGlyphCapacity; ++slot) {
            const auto glyph = GlyphBitmap(0x4E00 + (slot * 73) % 0x5000);
            for (unsigned row = 0; row < 16; ++row) pixels = pixels + glyph.Row(row);
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::printf("PASS: %zu font glyphs, stable views and bounds%s.\n",
                count, reference ? "; every pixel matches the reference" : "");
    std::printf("MEASURE: font=%zu bytes; view=%zu bytes; 640-glyph row reads=%.1f us/page (Host).\n",
                static_cast<std::size_t>(end - begin), sizeof(BitmapGlyph), elapsed / (repeats * 1000.0));
}
