#include "note4_display_model.h"
#include <cstring>
#include <type_traits>

namespace note4::display {
namespace {
struct Codec {
    uint8_t* output;
    const uint8_t* input;
    std::size_t cursor = 0;
    bool valid = true;
    template <typename T> void Field(T& field, unsigned width = sizeof(T)) {
        if (output)
            for (unsigned i = 0; i < width; ++i)
                output[cursor++] = static_cast<uint8_t>(field >> (8 * i));
        else {
            uint32_t value = 0;
            for (unsigned i = 0; i < width; ++i)
                value |= static_cast<uint32_t>(input[cursor++]) << (8 * i);
            if constexpr (std::is_same_v<T, bool>)
                if (value > 1)
                    valid = false;
            field = static_cast<T>(value);
        }
    }
    void Model(PhysicsParameters& p) {
        uint32_t version = 1;
        Field(version);
        if (version != 1)
            valid = false;
        Field(p.revision);
        Field(p.sample_max_age_ms);
        Field(p.window_weight_q8);
        Field(p.flip_weight_q8);
        Field(p.concentration_weight_q8);
        Field(p.memory_weight_q8);
        Field(p.global_limit_q16);
        Field(p.local_limit_q16);
        Field(p.memory_tau_ms);
        Field(p.debt_tau_ms);
        for (auto& gain : p.temperature_gain_q8)
            Field(gain);
        Field(p.unknown_temperature_gain_q8);
        Field(p.low_battery_mv);
        Field(p.low_battery_gain_q8);
        for (auto& e : p.energy) {
            Field(e.fixed_uj);
            Field(e.busy_power_uw);
            Field(e.spi_nj_per_byte);
            Field(e.black_to_white_nj);
            Field(e.white_to_black_nj);
            Field(e.calibrated, 1);
        }
    }
};
bool Valid(const PhysicsParameters& p) {
    PhysicsModel model;
    return model.SetParameters(p);
}
} // namespace
bool EncodeModel(const PhysicsParameters& p, uint8_t* bytes, std::size_t size) {
    if (!bytes || size != kModelBytes || !Valid(p))
        return false;
    auto copy = p;
    Codec codec{bytes, nullptr};
    codec.Model(copy);
    return codec.valid && codec.cursor == size;
}
bool DecodeModel(const uint8_t* bytes, std::size_t size, PhysicsParameters* out) {
    if (!bytes || !out || size != kModelBytes)
        return false;
    PhysicsParameters p;
    Codec codec{nullptr, bytes};
    codec.Model(p);
    if (!codec.valid || codec.cursor != size || !Valid(p))
        return false;
    *out = p;
    return true;
}
bool UpdateModel(PhysicsParameters* model, const char* group, const uint32_t* v,
                 std::size_t count) {
    if (!model || !group || !v || !count || count > 7 || model->revision == UINT32_MAX)
        return false;
    auto p = *model;
    const auto is = [&](const char* name, std::size_t n) {
        return count == n && !std::strcmp(group, name);
    };
    const auto small = [&] {
        for (std::size_t i = 0; i < count; ++i)
            if (v[i] > UINT16_MAX)
                return false;
        return true;
    };
    if (is("sample-age", 1))
        p.sample_max_age_ms = v[0];
    else if (is("weights", 4) && small()) {
        p.window_weight_q8 = v[0];
        p.flip_weight_q8 = v[1];
        p.concentration_weight_q8 = v[2];
        p.memory_weight_q8 = v[3];
    } else if (is("limits", 2)) {
        p.global_limit_q16 = v[0];
        p.local_limit_q16 = v[1];
    } else if (is("decay", 2)) {
        p.memory_tau_ms = v[0];
        p.debt_tau_ms = v[1];
    } else if (is("temperature", 5) && small())
        for (std::size_t i = 0; i < count; ++i)
            p.temperature_gain_q8[i] = v[i];
    else if (is("battery", 3) && small()) {
        p.low_battery_mv = v[0];
        p.low_battery_gain_q8 = v[1];
        p.unknown_temperature_gain_q8 = v[2];
    } else if (is("energy", 7) && v[0] >= 1 && v[0] <= 3 && v[3] <= UINT16_MAX &&
               v[4] <= UINT16_MAX && v[5] <= UINT16_MAX && v[6] <= 1)
        p.energy[v[0]] = {v[1],
                          v[2],
                          static_cast<uint16_t>(v[3]),
                          static_cast<uint16_t>(v[4]),
                          static_cast<uint16_t>(v[5]),
                          v[6] != 0};
    else
        return false;
    ++p.revision;
    if (!Valid(p))
        return false;
    *model = p;
    return true;
}
} // namespace note4::display
