#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace zectrix::package {

inline constexpr uint16_t kFormatVersion = 1, kGuestApi = 1;
inline constexpr uint16_t kDisplay = 1, kInput = 2;
inline constexpr uint32_t kSourceLimit = 32768, kInstructionLimit = 10000;
inline constexpr std::size_t kHeaderSize = 160, kIconLimit = 128;
inline constexpr uint32_t kPackageLimit = kHeaderSize + kIconLimit + kSourceLimit;

struct Metadata {
    std::array<char, 64> name{};
    std::array<char, 24> version{};
    std::array<char, 48> author{};
    std::array<uint8_t, kIconLimit> icon{};
    uint32_t source_size = 0, instructions = 0;
    uint16_t permissions = 0;
    uint8_t icon_side = 0;
    std::size_t IconBytes() const { return icon_side * icon_side / 8; }
    uint32_t SourceOffset() const { return kHeaderSize + IconBytes(); }
};

// UTF-8 is validated across transfer boundaries without retaining the source.
class TextValidator {
public:
    bool Feed(const uint8_t* data, std::size_t size, bool multiline = true) {
        if (!valid_ || (size && !data)) return valid_ = false;
        for (std::size_t i = 0; i < size; ++i) {
            const uint8_t c = data[i];
            if (remaining_) {
                if ((c & 0xc0) != 0x80) return valid_ = false;
                codepoint_ = (codepoint_ << 6) | (c & 63);
                if (!--remaining_ && (codepoint_ < minimum_ || codepoint_ > 0x10ffff ||
                    (codepoint_ >= 0xd800 && codepoint_ <= 0xdfff))) return valid_ = false;
            } else if (c < 0x80) {
                if ((c < 0x20 && !(multiline && (c == '\t' || c == '\n' || c == '\r'))) || c == 0x7f)
                    return valid_ = false;
            } else {
                remaining_ = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 :
                    c >= 0xf0 && c <= 0xf4 ? 3 : 0;
                if (!remaining_) return valid_ = false;
                minimum_ = remaining_ == 1 ? 0x80 : remaining_ == 2 ? 0x800 : 0x10000;
                codepoint_ = c & (remaining_ == 1 ? 31 : remaining_ == 2 ? 15 : 7);
            }
        }
        return true;
    }
    bool Complete() const { return valid_ && !remaining_; }
private:
    uint32_t codepoint_ = 0, minimum_ = 0;
    unsigned remaining_ = 0;
    bool valid_ = true;
};

inline uint32_t Read32(const uint8_t* p) {
    return uint32_t{p[0]} | (uint32_t{p[1]} << 8) | (uint32_t{p[2]} << 16) | (uint32_t{p[3]} << 24);
}
inline uint16_t Read16(const uint8_t* p) { return p[0] | (uint16_t{p[1]} << 8); }

template<std::size_t N>
bool ReadText(const uint8_t* data, std::array<char, N>& output, bool ascii = false) {
    std::size_t size = 0;
    while (size < N && data[size]) ++size;
    if (!size || size == N) return false;
    for (std::size_t i = size; i < N; ++i) if (data[i]) return false;
    if (ascii) for (std::size_t i = 0; i < size; ++i) if (data[i] < 0x21 || data[i] > 0x7e) return false;
    TextValidator text;
    if (!text.Feed(data, size, false) || !text.Complete()) return false;
    std::memcpy(output.data(), data, N);
    return true;
}

// Parse fixed-width fields explicitly; the wire layout never depends on C++ padding.
inline bool ReadHeader(const uint8_t* data, std::size_t size, uint32_t file_size, Metadata& output) {
    if (!data || size != kHeaderSize || std::memcmp(data, "ZAPP", 4) ||
        Read16(data + 4) != kFormatVersion || Read16(data + 6) != kGuestApi ||
        data[8] != 1 || (data[9] != 16 && data[9] != 32) || Read32(data + 20)) return false;
    Metadata meta;
    meta.icon_side = data[9];
    meta.permissions = Read16(data + 10);
    meta.instructions = Read32(data + 12);
    meta.source_size = Read32(data + 16);
    if ((meta.permissions & ~(kDisplay | kInput)) || meta.instructions < 100 ||
        meta.instructions > kInstructionLimit || !meta.source_size || meta.source_size > kSourceLimit ||
        file_size != meta.SourceOffset() + meta.source_size ||
        !ReadText(data + 24, meta.name) || !ReadText(data + 88, meta.version, true) ||
        !ReadText(data + 112, meta.author)) return false;
    output = meta;
    return true;
}

class Validator {
public:
    void Reset(uint32_t size) { *this = {}; expected_ = size; }
    bool Feed(const void* input, std::size_t size) {
        if (!valid_ || (size && !input) || size > expected_ - received_) return valid_ = false;
        const auto* data = static_cast<const uint8_t*>(input);
        while (size) {
            const auto end = received_ < kHeaderSize ? kHeaderSize : received_ < source_offset_ ? source_offset_ : expected_;
            const auto chunk = std::min<std::size_t>(size, end - received_);
            if (received_ < kHeaderSize) {
                std::memcpy(header_.data() + received_, data, chunk);
                if (received_ + chunk == kHeaderSize) {
                    Metadata meta;
                    if (!ReadHeader(header_.data(), header_.size(), expected_, meta)) return valid_ = false;
                    source_offset_ = meta.SourceOffset();
                }
            } else if (received_ >= source_offset_ && !source_.Feed(data, chunk)) return valid_ = false;
            data += chunk;
            size -= chunk;
            received_ += chunk;
        }
        return true;
    }
    bool Complete() const {
        return valid_ && source_offset_ && received_ == expected_ && source_.Complete();
    }
private:
    std::array<uint8_t, kHeaderSize> header_{};
    TextValidator source_;
    uint32_t expected_ = 0, received_ = 0, source_offset_ = 0;
    bool valid_ = true;
};

}  // namespace zectrix::package
