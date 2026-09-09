#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace zectrix::reader {

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
    uint32_t codepoint = 0;
    uint16_t x = 0;
    uint16_t y = 0;
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
int GlyphWidth(uint32_t codepoint, FontSize size);
// Each glyph has 16 big-endian, left-aligned bitmap rows after a width byte.
const uint8_t* GlyphBitmap(uint32_t codepoint);
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
