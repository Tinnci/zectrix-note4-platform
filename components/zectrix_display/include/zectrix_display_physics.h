#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "zectrix_display_state.h"

namespace zectrix::display {

inline constexpr std::size_t kPhysicsColumns = 5, kPhysicsRows = 4;
inline constexpr std::size_t kPhysicsTiles = kPhysicsColumns * kPhysicsRows;
inline constexpr uint32_t kTilePixels = 80 * 75;
inline constexpr uint32_t kDebtOne = 65536;

enum class RefreshKind : uint8_t { kNone, kFull1Bpp, kPartial1Bpp, kFull4Bpp };
enum class RefreshReason : uint8_t {
    Partial, Recovery, Quality, FullClean, HighContrast, GlobalDebt, LocalDebt, Gray, DriverError,
};

struct PixelTransitions {
    uint16_t black_to_white = 0;
    uint16_t white_to_black = 0;
};

struct FrameActivity {
    // The actual byte-aligned controller window, including unchanged neighbors.
    Rect window;
    std::array<PixelTransitions, kPhysicsTiles> tiles{};
    bool valid = false;
    uint32_t ChangedPixels() const;
};

struct PhysicsEnvironment {
    int16_t temperature_centi_c = 0;
    uint16_t battery_mv = 0;
    // UINT32_MAX means unavailable; age is measured on the monotonic clock.
    uint32_t temperature_age_ms = UINT32_MAX;
    uint32_t battery_age_ms = UINT32_MAX;
};

struct EnergyCoefficients {
    uint32_t fixed_uj = 0;
    uint32_t busy_power_uw = 0;
    uint16_t spi_nj_per_byte = 0;
    uint16_t black_to_white_nj = 0;
    uint16_t white_to_black_nj = 0;
    bool calibrated = false;
};

struct PhysicsParameters {
    uint32_t revision = 0;
    uint32_t sample_max_age_ms = 60000;
    // Q8.8 coefficients; Q16.16 debt is dimensionless, not optical contrast.
    uint16_t window_weight_q8 = 32;
    uint16_t flip_weight_q8 = 256;
    uint16_t concentration_weight_q8 = 256;
    uint16_t memory_weight_q8 = 64;
    uint32_t global_limit_q16 = kDebtOne * 3 / 4;
    uint32_t local_limit_q16 = kDebtOne * 4;
    uint32_t memory_tau_ms = 120000;
    // Zero disables decay: idle time alone cannot erase unmeasured ghosting.
    uint32_t debt_tau_ms = 0;
    // Piecewise-linear gains at -10, 0, 10, 25 and 40 degrees Celsius.
    std::array<uint16_t, 5> temperature_gain_q8{640, 512, 384, 256, 256};
    uint16_t unknown_temperature_gain_q8 = 384;
    uint16_t low_battery_mv = 3500;
    uint16_t low_battery_gain_q8 = 320;
    std::array<EnergyCoefficients, 4> energy{};
};

struct DebtState {
    std::array<uint32_t, kPhysicsTiles> debt_q16{};
    std::array<int32_t, kPhysicsTiles> memory_q16{};
    uint32_t Mean() const;
    uint32_t Peak() const;
};

struct PhysicsPrediction {
    DebtState next;
    RefreshReason reason = RefreshReason::Partial;
    uint16_t gain_q8 = 256;
    bool RequiresFull() const { return reason != RefreshReason::Partial; }
};

// All access belongs to the display owner. Prediction is pure; only a
// successful physical transaction, including rail cleanup, commits its debt.
class PhysicsModel {
public:
    const PhysicsParameters& parameters() const { return parameters_; }
    const DebtState& state() const { return state_; }
    bool SetParameters(const PhysicsParameters& parameters);
    PhysicsPrediction Predict(const FrameActivity& activity,
                              const PhysicsEnvironment& environment, uint64_t now_us) const;
    void Commit(const PhysicsPrediction& prediction, uint64_t completed_us);
    void Clean(uint64_t completed_us);
    uint16_t EnvironmentGain(const PhysicsEnvironment& environment) const;
    bool EstimateEnergy(RefreshKind kind, uint32_t busy_us, uint32_t spi_bytes,
                        bool transitions_valid, uint32_t black_to_white,
                        uint32_t white_to_black, uint32_t* energy_uj) const;

private:
    PhysicsParameters parameters_;
    DebtState state_;
    uint64_t completed_us_ = 0;
};

}  // namespace zectrix::display
