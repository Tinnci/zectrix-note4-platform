#pragma once
#include <cstddef>
#include <cstdint>

namespace note4::storage::edge {
inline constexpr char kName[] = "page.bin";
constexpr std::size_t kHeaderSize = 32;
constexpr std::size_t kPixelBytes = 15000;
constexpr std::size_t kFileSize = kHeaderSize + kPixelBytes;
struct Page {
    uint32_t revision = 0;
    uint32_t issued_at = 0, expires_at = 0, next_sync_at = 0;
    bool portrait = true;
    uint32_t source_generation = 0;  // Server sends zero; cache stamps the local source version.
    bool Fresh(int64_t now) const { return now >= issued_at && now < expires_at; }
};
// ZEP1, little-endian integers, physical or tightly packed logical 1bpp.
inline uint32_t Read32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline bool Decode(const uint8_t* data, std::size_t size, Page* page) {
    if (!data || !page || size != kFileSize || data[0] != 'Z' || data[1] != 'E' ||
        data[2] != 'P' || data[3] != '1' || data[4] > 1) return false;
    for (unsigned i = 5; i < 8; ++i) if (data[i]) return false;
    for (unsigned i = 28; i < 32; ++i) if (data[i]) return false;
    *page = {Read32(data + 8), Read32(data + 12), Read32(data + 16), Read32(data + 20), data[4] == 1};
    page->source_generation = Read32(data + 24);
    return page->revision != 0 && page->issued_at >= 1704067200 &&
        page->expires_at > page->issued_at && page->expires_at - page->issued_at <= 7 * 86400 &&
        (page->next_sync_at == 0 || (page->next_sync_at > page->issued_at && page->next_sync_at <= page->expires_at));
}
}  // namespace note4::storage::edge
