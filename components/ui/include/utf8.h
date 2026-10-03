#pragma once

#include <cstdint>

namespace zectrix::ui {
// Consume one scalar, replacing malformed sequences without reading past NUL.
inline uint32_t NextUtf8(const char*& text) {
    const auto first = static_cast<uint8_t>(*text);
    if (!first) return 0;
    ++text;
    if (first < 0x80) return first;
    if (first < 0xc2 || first > 0xf4) return 0xfffd;
    const unsigned count = first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
    uint32_t cp = first & (count == 2 ? 31 : count == 3 ? 15 : 7);
    for (unsigned i = 1; i < count; ++i) {
        const auto byte = static_cast<uint8_t>(*text);
        if (byte < 0x80 || byte > 0xbf) return 0xfffd;
        ++text;
        cp = (cp << 6) | (byte & 63);
    }
    return cp < (count == 2 ? 0x80U : count == 3 ? 0x800U : 0x10000U) ||
        cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) ? 0xfffd : cp;
}
}  // namespace zectrix::ui
