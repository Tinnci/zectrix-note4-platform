#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "zectrix/sdk/text_style.h"

namespace zectrix::reader {
using sdk::TextStyle;

enum class Result : uint8_t {
    Ok, Pending, End, Invalid, Unsupported, TooLarge, IoError, NoMemory,
};
const char* ResultName(Result result);

// Sources stay open for the engine lifetime. Reads are exact and never grow
// a buffer to the size of a book, chapter, or ZIP member.
class Source {
public:
    virtual ~Source() = default;
    virtual uint32_t Size() const = 0;
    virtual bool Read(uint32_t offset, void* output, std::size_t size) = 0;
};

class MemorySource final : public Source {
public:
    MemorySource(const uint8_t* data, uint32_t size) : data_(data), size_(size) {}
    uint32_t Size() const override { return size_; }
    bool Read(uint32_t offset, void* output, std::size_t size) override;
private:
    const uint8_t* data_;
    uint32_t size_;
};

enum class Format : uint8_t { Text, Epub };
enum class FontSize : uint8_t { Small, Large };

// EPUB offsets refer to uncompressed XHTML bytes in spine order. Text uses
// chapter zero and UTF-8 byte offsets. They do not depend on pagination.
struct Position {
    uint32_t offset = 0;
    uint16_t chapter = 0;
    bool operator==(Position other) const { return chapter == other.chapter && offset == other.offset; }
    bool operator!=(Position other) const { return !(*this == other); }
    bool operator<(Position other) const {
        return chapter < other.chapter || (chapter == other.chapter && offset < other.offset);
    }
};

struct Glyph {
    // Unicode needs 21 bits; styles share the existing eight-byte page entry.
    uint32_t codepoint : 24;
    TextStyle style : 8;
    uint16_t x;
    uint16_t y;
    constexpr Glyph(uint32_t cp = 0, uint16_t left = 0, uint16_t top = 0,
                    TextStyle appearance = TextStyle::Regular)
        : codepoint(cp), style(appearance), x(left), y(top) {}
};

struct Page {
    static constexpr int kWidth = 384;
    static constexpr int kHeight = 216;
    static constexpr std::size_t kGlyphCapacity = 640;
    std::array<Glyph, kGlyphCapacity> glyphs{};
    std::size_t count = 0;
    Position start{};
    Position next{};
    FontSize font = FontSize::Small;
    uint16_t progress_per_mille = 0;
    bool end = false;
};

int FontHeight(FontSize size);
int GlyphWidth(uint32_t codepoint, FontSize size, TextStyle style = TextStyle::Regular);
int GlyphHeight(uint32_t codepoint, FontSize size, TextStyle style = TextStyle::Regular);
// Immutable flash views stay valid across later glyph lookups. No cache or
// decompression buffer is needed; each row combines two shared 8x8 tiles.
struct BitmapGlyph {
    std::array<const uint8_t*, 4> tiles{};
    uint8_t width = 0;
    uint16_t Row(unsigned row) const {
        if (row >= 16 || width == 0) return 0;
        const unsigned pair = (row / 8) * 2;
        return static_cast<uint16_t>(tiles[pair][row % 8]) << 8 | tiles[pair + 1][row % 8];
    }
};
BitmapGlyph GlyphBitmap(uint32_t codepoint);
bool IsCjk(uint32_t codepoint);

class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // EPUB metadata opens cooperatively: Poll Pending to completion, then Seek.
    Result Open(Source& source, Format format);
    void Close();
    Result Seek(Position position, FontSize font);
    Result Next();
    Result Previous();
    Result SetFont(FontSize font);
    // Bound decoding steps per owner callback. A step consumes one body byte
    // or up to four UTF-8 context bytes; ZIP lookups account for header bytes.
    // Fixed-buffer I/O may read ahead by one entry or input block.
    // A single inflate call produces at most its fixed 32 KiB dictionary.
    Result Poll(std::size_t byte_budget = 4096);
    void Cancel();
    bool busy() const;
    bool has_page() const;
    const Page& page() const;
    uint16_t chapters() const;
    bool ValidPosition(Position position) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace zectrix::reader
