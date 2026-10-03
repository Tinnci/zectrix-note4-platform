#pragma once
#include <array>
#include <cstdint>
namespace note4::connectivity {
// BTHome v2 service data, including UUID. Irregular wake bursts are trigger-based.
// Public telemetry only; never use these unauthenticated packets for commands.
inline std::array<uint8_t, 12> BTHomePower(uint8_t packet, uint8_t percent,
                                        uint16_t millivolts, bool charging) {
    return {0xd2, 0xfc, 0x44, 0x00, packet, 0x01,
            static_cast<uint8_t>(percent > 100 ? 100 : percent), 0x0c,
            static_cast<uint8_t>(millivolts), static_cast<uint8_t>(millivolts >> 8),
            0x16, static_cast<uint8_t>(charging)};
}
}  // namespace note4::connectivity
