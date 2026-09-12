#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace zectrix::companion {

constexpr uint16_t kWeatherSyncKey = 0x0102;
struct WeatherSnapshot {
    std::array<char, 49> place{};
    int16_t deci_celsius = 0;
    uint8_t code = 0;
    uint32_t observed_at = 0, expires_at = 0;
    bool Fresh(int64_t now) const {
        return now >= static_cast<int64_t>(observed_at) - 300 && now < expires_at;
    }
};

inline bool DecodeWeatherSnapshot(const uint8_t* data, std::size_t size, WeatherSnapshot* output) {
    if (!data || !output || size < 14 || size > 61 || data[0] != 1 || data[12] != size - 13) return false;
    constexpr uint8_t codes[] = {0, 1, 2, 3, 45, 48, 51, 53, 55, 56, 57, 61, 63, 65, 66, 67,
        71, 73, 75, 77, 80, 81, 82, 85, 86, 95, 96, 99};
    bool valid_code = false;
    for (const auto code : codes) if (data[1] == code) valid_code = true;
    if (!valid_code) return false;
    const auto read32 = [](const uint8_t* p) {
        return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    };
    WeatherSnapshot value;
    value.code = data[1];
    const int raw = data[2] | unsigned(data[3]) << 8;
    value.deci_celsius = raw >= 32768 ? raw - 65536 : raw;
    value.observed_at = read32(data + 4);
    value.expires_at = read32(data + 8);
    if (value.deci_celsius < -1000 || value.deci_celsius > 1000 || value.observed_at < 946684800U ||
        value.expires_at <= value.observed_at || value.expires_at > 4102444800U ||
        value.expires_at - value.observed_at > 21600) return false;
    // Labels are bounded UTF-8 text, never control sequences or unterminated input.
    for (std::size_t i = 13; i < size; ++i) {
        const uint8_t c = data[i];
        if (c < 32 || c == 127) return false;
        if (c < 128) continue;
        const unsigned extra = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 : c >= 0xf0 && c <= 0xf4 ? 3 : 0;
        if (!extra || extra >= size - i) return false;
        uint32_t cp = c & (extra == 1 ? 31 : extra == 2 ? 15 : 7);
        for (unsigned n = 0; n < extra; ++n) {
            if ((data[++i] & 0xc0) != 0x80) return false;
            cp = cp << 6 | (data[i] & 63);
        }
        if (cp < (extra == 1 ? 0x80U : extra == 2 ? 0x800U : 0x10000U) ||
            cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    }
    std::memcpy(value.place.data(), data + 13, size - 13);
    *output = value;
    return true;
}

}  // namespace zectrix::companion
