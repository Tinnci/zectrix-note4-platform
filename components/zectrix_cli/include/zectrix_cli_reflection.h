#pragma once

#include <array>
#include <cstdint>

#include "zectrix_input_trace.h"
#include "zectrix_time_sync.h"

namespace zectrix::cli {

struct PowerInspection {
    int64_t age_ms = -1;
    uint16_t millivolts = 0;
    uint8_t percent = 0;
    bool valid = false, external = false, charging = false, full = false, fault = false, absent = false;
};

struct TimeInspection {
    int64_t unix_seconds = 0, accepted_age_ms = -1;
    std::array<int16_t, 6> local{};
    int32_t offset_seconds = 0, error = 0;
    time::SyncStatus sync{};
    bool calendar_valid = false, offset_known = false, rtc_available = false, persisted = false, pending = false;
};

struct ConnectivityInspection {
    std::array<char, 24> ble{}, wifi{}, radio{};
    std::array<char, 33> ssid{};
    std::array<char, 16> address{};
    std::array<uint8_t, 6> mac{};
    uint32_t session = 0;
    int8_t rssi = -127;
    uint8_t mode = 0;
    bool encrypted = false, authenticated = false, bonded = false, authorized = false;
    bool negotiated = false, pairing = false, transfer = false, busy = false;
    bool rssi_valid = false, mac_valid = false;
};

struct AppInspection {
    struct Entry { std::array<char, 32> id{}, label{}; };
    std::array<Entry, 16> entries{};
    std::array<char, 32> foreground{};
    uint32_t generation = 0;
    uint8_t count = 0, lifecycle = 0, error = 0;
};

struct SceneInspection {
    struct Scene { uint32_t state = 0; uint8_t id = 0; };
    struct View {
        int16_t x = 0, y = 0, width = 0, height = 0;
        bool configured = false, enabled = false, dirty = false, quality = false;
    };
    std::array<Scene, 8> scenes{};
    std::array<View, 4> views{};
    std::array<char, 64> guest_name{};
    uint32_t heap_live = 0, heap_peak = 0, heap_limit = 0, heap_rejected = 0, instruction_limit = 0;
    uint8_t depth = 0;
    bool transitioning = false, guest = false;
};

}  // namespace zectrix::cli
