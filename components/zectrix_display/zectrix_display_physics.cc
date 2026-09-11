#include "zectrix_display_physics.h"

#include <algorithm>

namespace zectrix::display {
namespace {

constexpr uint32_t kDebtMaximum = 64 * kDebtOne;
constexpr int16_t kTemperatures[] = {-1000, 0, 1000, 2500, 4000};

uint32_t Saturate(uint64_t value) { return std::min<uint64_t>(value, kDebtMaximum); }
uint32_t Multiply(uint32_t left, uint32_t right) {
    return Saturate(static_cast<uint64_t>(left) * right / kDebtOne);
}

uint32_t Retain(uint32_t value, uint32_t tau_ms, uint64_t elapsed_ms) {
    if (!tau_ms) return value;
    // A backward-Euler RC step stays positive for arbitrarily long idle gaps.
    return static_cast<uint64_t>(value) * tau_ms / (tau_ms + elapsed_ms);
}

uint32_t DrivenPixels(const Rect& window, std::size_t tile) {
    const int x = (tile % kPhysicsColumns) * 80;
    const int y = (tile / kPhysicsColumns) * 75;
    // Clip first so even a malformed host observation cannot overflow x + width.
    const int left = std::clamp(window.x, 0, 400), top = std::clamp(window.y, 0, 300);
    const int right = left + std::clamp(window.width, 0, 400 - left);
    const int bottom = top + std::clamp(window.height, 0, 300 - top);
    return std::max(0, std::min(x + 80, right) - std::max(x, left)) *
           std::max(0, std::min(y + 75, bottom) - std::max(y, top));
}

}  // namespace

uint32_t FrameActivity::ChangedPixels() const {
    uint32_t result = 0;
    for (const auto& tile : tiles) result += tile.black_to_white + tile.white_to_black;
    return result;
}

uint32_t DebtState::Mean() const {
    uint64_t total = 0;
    for (auto debt : debt_q16) total += debt;
    return total / kPhysicsTiles;
}

uint32_t DebtState::Peak() const {
    return *std::max_element(debt_q16.begin(), debt_q16.end());
}

bool PhysicsModel::SetParameters(const PhysicsParameters& p) {
    if (!p.global_limit_q16 || p.global_limit_q16 > kDebtMaximum ||
        !p.local_limit_q16 || p.local_limit_q16 > kDebtMaximum ||
        !p.sample_max_age_ms || p.sample_max_age_ms == UINT32_MAX ||
        !p.flip_weight_q8 || !p.unknown_temperature_gain_q8 || !p.low_battery_gain_q8) return false;
    for (auto gain : p.temperature_gain_q8) if (!gain) return false;
    // Existing debt retains its units and history when calibration changes.
    parameters_ = p;
    return true;
}

uint16_t PhysicsModel::EnvironmentGain(const PhysicsEnvironment& e) const {
    const auto& p = parameters_;
    uint32_t gain = p.unknown_temperature_gain_q8;
    if (e.temperature_age_ms <= p.sample_max_age_ms &&
        e.temperature_centi_c >= -4000 && e.temperature_centi_c <= 8500) {
        const int temperature = e.temperature_centi_c;
        gain = p.temperature_gain_q8.back();
        if (temperature <= kTemperatures[0]) gain = p.temperature_gain_q8[0];
        else for (std::size_t index = 1; index < p.temperature_gain_q8.size(); ++index) {
            if (temperature > kTemperatures[index]) continue;
            const int range = kTemperatures[index] - kTemperatures[index - 1];
            const int offset = temperature - kTemperatures[index - 1];
            gain = (static_cast<uint32_t>(p.temperature_gain_q8[index - 1]) * (range - offset) +
                    static_cast<uint32_t>(p.temperature_gain_q8[index]) * offset) / range;
            break;
        }
    }
    if (e.battery_age_ms <= p.sample_max_age_ms && e.battery_mv && e.battery_mv < p.low_battery_mv)
        gain = static_cast<uint64_t>(gain) * p.low_battery_gain_q8 / 256;
    return std::clamp<uint32_t>(gain, 1, UINT16_MAX);
}

PhysicsPrediction PhysicsModel::Predict(const FrameActivity& activity,
                                         const PhysicsEnvironment& environment, uint64_t now_us) const {
    PhysicsPrediction prediction;
    prediction.next = state_;
    prediction.gain_q8 = EnvironmentGain(environment);
    if (!activity.valid) {
        prediction.reason = RefreshReason::Recovery;
        return prediction;
    }
    if (!activity.ChangedPixels()) return prediction;
    const auto& p = parameters_;
    const uint64_t elapsed_ms = now_us >= completed_us_ ? (now_us - completed_us_) / 1000 : 0;
    const uint32_t gain = static_cast<uint32_t>(prediction.gain_q8) * 256;
    for (std::size_t index = 0; index < kPhysicsTiles; ++index) {
        const auto& tile = activity.tiles[index];
        const uint32_t flips = std::min<uint32_t>(tile.black_to_white + tile.white_to_black, kTilePixels);
        const uint32_t density = flips * kDebtOne / kTilePixels;
        const uint32_t area = DrivenPixels(activity.window, index) * kDebtOne / kTilePixels;
        const int64_t imbalance = (static_cast<int32_t>(tile.white_to_black) - tile.black_to_white) *
                                  static_cast<int64_t>(kDebtOne) / kTilePixels;
        const int32_t old_memory = state_.memory_q16[index];
        const uint32_t retained = Retain(old_memory < 0 ? -old_memory : old_memory, p.memory_tau_ms, elapsed_ms);
        const int64_t memory = (old_memory < 0 ? -static_cast<int64_t>(retained) : retained) +
                               imbalance * gain / kDebtOne;
        prediction.next.memory_q16[index] = std::clamp<int64_t>(memory, -static_cast<int64_t>(kDebtMaximum), kDebtMaximum);
        const int32_t next_memory = prediction.next.memory_q16[index];
        const uint32_t magnitude = next_memory < 0 ? -next_memory : next_memory;
        const uint64_t weighted = static_cast<uint64_t>(p.window_weight_q8) * area +
            static_cast<uint64_t>(p.flip_weight_q8) * density +
            static_cast<uint64_t>(p.concentration_weight_q8) * Multiply(density, density) +
            static_cast<uint64_t>(p.memory_weight_q8) * Multiply(magnitude, density);
        // Round positive stress upward so tiny calibrated coefficients cannot
        // make repeated one-pixel transitions disappear into a quantization gap.
        const uint32_t increment = Saturate((weighted * prediction.gain_q8 + 65535) / 65536);
        prediction.next.debt_q16[index] = Saturate(
            static_cast<uint64_t>(Retain(state_.debt_q16[index], p.debt_tau_ms, elapsed_ms)) + increment);
    }
    if (activity.ChangedPixels() >= StateModel::kHighContrastPixelLimit)
        prediction.reason = RefreshReason::HighContrast;
    else if (prediction.next.Peak() >= p.local_limit_q16)
        prediction.reason = RefreshReason::LocalDebt;
    else if (prediction.next.Mean() >= p.global_limit_q16)
        prediction.reason = RefreshReason::GlobalDebt;
    return prediction;
}

void PhysicsModel::Commit(const PhysicsPrediction& prediction, uint64_t completed_us) {
    state_ = prediction.next;
    completed_us_ = completed_us;
}

void PhysicsModel::Clean(uint64_t completed_us) {
    state_ = {};
    completed_us_ = completed_us;
}

bool PhysicsModel::EstimateEnergy(RefreshKind kind, uint32_t busy_us, uint32_t spi_bytes,
                                  bool transitions_valid, uint32_t black_to_white,
                                  uint32_t white_to_black, uint32_t* energy_uj) const {
    if (!energy_uj) return false;
    *energy_uj = 0;
    const auto mode = static_cast<std::size_t>(kind);
    if (mode == 0 || mode >= parameters_.energy.size()) return false;
    const auto& e = parameters_.energy[mode];
    if (!e.calibrated || (!transitions_valid && (e.black_to_white_nj || e.white_to_black_nj))) return false;
    const uint64_t result = e.fixed_uj + static_cast<uint64_t>(e.busy_power_uw) * busy_us / 1000000 +
        (static_cast<uint64_t>(e.spi_nj_per_byte) * spi_bytes +
         static_cast<uint64_t>(e.black_to_white_nj) * black_to_white +
         static_cast<uint64_t>(e.white_to_black_nj) * white_to_black) / 1000;
    *energy_uj = std::min<uint64_t>(result, UINT32_MAX);
    return true;
}

}  // namespace zectrix::display
