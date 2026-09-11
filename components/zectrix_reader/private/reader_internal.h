#pragma once

#include "zectrix_reader.h"
#include "miniz_tinfl.h"

namespace zectrix::reader::detail {

constexpr std::size_t kPathCapacity = 192;
constexpr uint16_t kChapterCapacity = 128;
constexpr uint32_t kMetadataLimit = 256 * 1024;
constexpr uint32_t kChapterLimit = 16 * 1024 * 1024;

uint16_t Le16(const uint8_t* p);
uint32_t Le32(const uint8_t* p);

struct Entry {
    uint32_t local = 0;
    uint32_t compressed = 0;
    uint32_t size = 0;
    uint32_t crc = 0;
    uint16_t method = 0;
    uint16_t flags = 0;
};

class Zip {
public:
    Result Open(Source& source);
    Result Find(const char* path);
    Result Poll(Entry* entry, std::size_t* budget);
private:
    Source* source_ = nullptr;
    uint32_t directory_ = 0;
    uint32_t directory_end_ = 0;
    uint16_t entries_ = 0;
    std::array<char, kPathCapacity> path_{};
    uint32_t offset_ = 0;
    uint16_t index_ = 0;
    Entry found_entry_{};
    bool found_ = false;
};

class Stream {
public:
    Result Open(Source& source, const Entry& entry, bool zipped, uint32_t text_offset = 0);
    Result Byte(uint8_t* output);
    uint32_t offset() const { return consumed_; }
private:
    Result Fill();
    Source* source_ = nullptr;
    Entry entry_{};
    tinfl_decompressor inflater_{};
    std::array<uint8_t, 32768> dictionary_{};
    std::array<uint8_t, 1024> input_{};
    uint32_t data_offset_ = 0;
    uint32_t input_loaded_ = 0;
    uint32_t produced_ = 0;
    uint32_t consumed_ = 0;
    uint32_t crc_ = 0;
    std::size_t input_pos_ = 0;
    std::size_t input_size_ = 0;
    std::size_t output_pos_ = 0;
    std::size_t output_end_ = 0;
    bool zipped_ = false;
    bool done_ = false;
};

struct XmlEvent {
    enum class Kind : uint8_t { None, Byte, Scalar, Tag };
    Kind kind = Kind::None;
    uint32_t value = 0;
    uint32_t start = 0;
};

// A byte-at-a-time lexer keeps comments, attributes, and hidden content
// cancellable even when the input contains no printable text for megabytes.
class Xml {
public:
    Result Feed(uint8_t byte, uint32_t offset, XmlEvent* event);
    void Reset();
    bool complete() const;
    const char* tag() const { return buffer_.data(); }
private:
    enum class State : uint8_t { Text, Tag, Entity, Comment, Declaration };
    State state_ = State::Text;
    std::array<char, 1024> buffer_{};
    std::size_t size_ = 0;
    uint32_t start_ = 0;
    uint8_t quote_ = 0;
    uint8_t dashes_ = 0;
    unsigned brackets_ = 0;
};

bool TagIs(const char* tag, const char* name, bool closing = false);
bool Attribute(const char* tag, const char* name, char* output, std::size_t capacity);
bool ResolvePath(const char* base, const char* href, char* output, std::size_t capacity);

struct Chapter {
    Entry entry{};
    std::array<char, 64> id{};
    bool found = false;
};

class Book {
public:
    Result Open(Source& source, Format format);
    Result PollOpen(std::size_t budget);
    Result Start(uint16_t chapter, uint32_t text_offset = 0);
    Source* source = nullptr;
    Format format = Format::Text;
    std::array<Chapter, kChapterCapacity> sections{};
    uint16_t count = 0;
    uint64_t total = 0;
    Stream stream;
private:
    enum class Phase : uint8_t { MimeLookup, Mime, EncryptionLookup, ContainerLookup, Container,
        PackageLookup, Spine, Manifest, ChapterLookup, Ready };
    Result Tag(const char* tag);
    Result EndMetadata();
    Zip zip_;
    Xml xml_;
    Entry package_{};
    std::array<char, kPathCapacity> package_path_{};
    Phase phase_ = Phase::Ready;
    uint16_t resolving_ = 0;
    uint8_t mime_offset_ = 0;
    bool in_spine_ = false;
    bool in_manifest_ = false;
};

struct Token {
    uint32_t codepoint = 0;
    Position start{};
    Position after{};
    bool paragraph = false;
    TextStyle style = TextStyle::Regular;
};

class Decoder {
public:
    void Reset(uint16_t chapter, Format format);
    Result Step(Stream& stream, Token* token, bool* emitted);
private:
    bool Emit(uint32_t codepoint, uint32_t start, uint32_t after, Token* token);
    void StyleTag(const char* tag);
    Xml xml_{};
    Format format_ = Format::Text;
    uint16_t chapter_ = 0;
    uint32_t utf8_value_ = 0;
    uint32_t utf8_start_ = 0;
    uint32_t utf8_min_ = 0;
    uint8_t utf8_remaining_ = 0;
    std::array<char, 16> hidden_{};
    unsigned hidden_depth_ = 0;
    bool paragraph_ = true;
    bool space_ = true;
    bool cr_ = false;
    bool pending_ = false;
    XmlEvent pending_event_{};
    uint32_t pending_after_ = 0;
    std::array<uint8_t, 16> styles_{};
    uint8_t style_count_ = 0;
    uint32_t style_overflow_ = 0;
    TextStyle style_ = TextStyle::Regular;
};

}  // namespace zectrix::reader::detail
