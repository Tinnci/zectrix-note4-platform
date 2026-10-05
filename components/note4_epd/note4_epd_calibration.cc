#include "note4_epd_calibration.h"

#include "ssd2683_waveform.h"
#include <algorithm>

namespace {
void Put16(uint8_t* out, uint16_t value) {
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8);
}
uint16_t Get16(const uint8_t* in) { return in[0] | (uint16_t(in[1]) << 8); }
bool SameRecipe(const note4_epd_gray_level_t& a, const note4_epd_gray_level_t& b) {
    // Compare selected whole records, not selector spellings. The terminator
    // is identical in every vendor table and cannot distinguish two levels.
    for (size_t record = 0; record < 7; ++record) {
        const size_t ta = (a.alternate_mask & (1u << record)) ? a.alternate_table : a.base_table;
        const size_t tb = (b.alternate_mask & (1u << record)) ? b.alternate_table : b.base_table;
        for (size_t byte = 0; byte < ssd2683_waveform::kRecordSize; ++byte) {
            const size_t offset =
                ssd2683_waveform::kAnalogConfigSize + record * ssd2683_waveform::kRecordSize + byte;
            if (ssd2683_waveform::kVendorGray4Waveform[offset +
                                                       ta * ssd2683_waveform::kTableSize] !=
                ssd2683_waveform::kVendorGray4Waveform[offset + tb * ssd2683_waveform::kTableSize])
                return false;
        }
    }
    return true;
}
} // namespace

extern "C" void note4_epd_calibration_default(note4_epd_calibration_t* out) {
    if (!out)
        return;
    *out = {};
    out->version = NOTE4_EPD_CALIBRATION_VERSION;
    out->panel = NOTE4_EPD_CALIBRATION_PANEL;
    out->revision = 1;
    for (size_t i = 0; i < NOTE4_EPD_GRAY_LEVELS; ++i)
        out->levels[i] = ssd2683_waveform::kDefaultLevels[i];
    // Historical measurements include a predicted level and a different hold
    // count at the light end. Do not claim current-sequence calibration.
    out->measured_levels = 0;
}

extern "C" bool note4_epd_calibration_validate(const note4_epd_calibration_t* p) {
    if (!p || p->version != NOTE4_EPD_CALIBRATION_VERSION ||
        p->panel != NOTE4_EPD_CALIBRATION_PANEL || !p->revision)
        return false;
    const auto& white = p->levels[15];
    if (p->levels[0].reflectance_permille != 0 || white.reflectance_permille != 1000 ||
        white.base_table != 5 || white.alternate_table != 5 || white.alternate_mask != 0)
        return false;
    const auto& black = p->levels[0];
    if (black.base_table != 1 || black.alternate_table != 1 || black.alternate_mask != 0)
        return false;
    for (size_t i = 0; i < 15; ++i) {
        const auto& level = p->levels[i];
        if (level.base_table < 1 || level.base_table > 4 || level.alternate_table < 1 ||
            level.alternate_table > 4 || (level.alternate_mask & 0x80))
            return false;
        if (level.reflectance_permille >= p->levels[i + 1].reflectance_permille)
            return false;
    }
    for (size_t i = 0; i < 15; ++i) {
        for (size_t j = i + 1; j < 15; ++j) {
            if (SameRecipe(p->levels[i], p->levels[j]))
                return false;
        }
    }
    return true;
}

extern "C" bool note4_epd_calibration_encode(const note4_epd_calibration_t* p, uint8_t* bytes,
                                             size_t size) {
    if (!bytes || size != NOTE4_EPD_CALIBRATION_BYTES || !note4_epd_calibration_validate(p))
        return false;
    Put16(bytes, p->version);
    Put16(bytes + 2, p->panel);
    Put16(bytes + 4, static_cast<uint16_t>(p->revision));
    Put16(bytes + 6, static_cast<uint16_t>(p->revision >> 16));
    Put16(bytes + 8, p->measured_levels);
    Put16(bytes + 10, 0);
    for (size_t i = 0; i < NOTE4_EPD_GRAY_LEVELS; ++i) {
        auto* out = bytes + 12 + i * 5;
        const auto& level = p->levels[i];
        out[0] = level.base_table;
        out[1] = level.alternate_table;
        out[2] = level.alternate_mask;
        Put16(out + 3, level.reflectance_permille);
    }
    return true;
}

extern "C" bool note4_epd_calibration_decode(const uint8_t* bytes, size_t size,
                                             note4_epd_calibration_t* out) {
    if (!bytes || !out || size != NOTE4_EPD_CALIBRATION_BYTES || Get16(bytes + 10) != 0)
        return false;
    note4_epd_calibration_t p{};
    p.version = Get16(bytes);
    p.panel = Get16(bytes + 2);
    p.revision = Get16(bytes + 4) | (uint32_t(Get16(bytes + 6)) << 16);
    p.measured_levels = Get16(bytes + 8);
    for (size_t i = 0; i < NOTE4_EPD_GRAY_LEVELS; ++i) {
        const auto* in = bytes + 12 + i * 5;
        p.levels[i] = {in[0], in[1], in[2], Get16(in + 3)};
    }
    if (!note4_epd_calibration_validate(&p))
        return false;
    *out = p;
    return true;
}

extern "C" bool note4_epd_calibration_update(note4_epd_calibration_t* p, uint8_t level,
                                             const note4_epd_gray_level_t* value, bool measured) {
    if (!value || level >= NOTE4_EPD_GRAY_LEVELS || !note4_epd_calibration_validate(p) ||
        p->revision == UINT32_MAX)
        return false;
    auto next = *p;
    // Validate before recipe comparison so malformed table indices never read
    // outside the vendor payload.
    next.levels[level] = *value;
    ++next.revision;
    if (!note4_epd_calibration_validate(&next))
        return false;
    if (!SameRecipe(p->levels[level], *value))
        next.measured_levels = 0;
    next.measured_levels &= static_cast<uint16_t>(~(1u << level));
    if (measured)
        next.measured_levels |= static_cast<uint16_t>(1u << level);
    *p = next;
    return true;
}

extern "C" uint8_t note4_epd_calibration_quantize(const note4_epd_calibration_t* p,
                                                  uint16_t value) {
    if (!p || p->version != NOTE4_EPD_CALIBRATION_VERSION ||
        p->panel != NOTE4_EPD_CALIBRATION_PANEL)
        return 15;
    value = std::min<uint16_t>(1000, value);
    uint8_t low = 0, high = 15;
    while (low < high) {
        const uint8_t middle = (low + high) / 2;
        // Binary search ordered optical midpoints, with no division/rounding
        // of odd sums. Exact ties select the lighter (higher) level.
        const unsigned sum =
            p->levels[middle].reflectance_permille + p->levels[middle + 1].reflectance_permille;
        if (unsigned(value) * 2 < sum)
            high = middle;
        else
            low = middle + 1;
    }
    return low;
}
