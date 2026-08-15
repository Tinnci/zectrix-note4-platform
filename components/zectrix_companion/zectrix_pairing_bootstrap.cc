#include "zectrix_pairing_bootstrap.h"

#include <algorithm>
#include <cstring>

namespace zectrix::companion {
namespace {

// The monotonic clock is a 32-bit millisecond counter. DeadlineReached uses
// signed modular arithmetic that is valid only when the configured durations
// are below 2^31 ms. Clamp public configuration into that envelope.
constexpr uint32_t kMaximumDurationMs = 0x7fffffffU;
constexpr uint32_t kDefaultDurationMs = 120000;

uint32_t SanitizeDuration(uint32_t value) {
    if (value == 0) return kDefaultDurationMs;
    return std::min(value, kMaximumDurationMs);
}

}  // namespace

PairingBootstrap::PairingBootstrap(PairingBootstrapClock& clock,
                                   PairingBootstrapRandom& random,
                                   BootstrapConfig config)
    : clock_(clock), random_(random), config_(config) {
    config_.token_ttl_ms = SanitizeDuration(config_.token_ttl_ms);
    config_.pairing_window_ms = SanitizeDuration(config_.pairing_window_ms);
}

PairingBootstrap::~PairingBootstrap() { token_.fill(0); }

BootstrapStatus PairingBootstrap::Prepare() {
    BootstrapToken generated{};
    if (!random_.Fill(&generated)) {
        return BootstrapStatus::kInvalidState;
    }
    token_ = generated;
    const uint32_t now = clock_.MonotonicMilliseconds();
    ++generation_;
    if (generation_ == 0) ++generation_;
    session_id_ = 0;
    prepared_at_ms_ = now;
    token_expires_at_ms_ = now + config_.token_ttl_ms;
    pairing_window_expires_at_ms_ = 0;
    state_ = BootstrapState::kPrepared;
    return BootstrapStatus::kOk;
}

BootstrapStatus PairingBootstrap::OpenPairingWindow() {
    if (state_ != BootstrapState::kPrepared &&
        state_ != BootstrapState::kPairingWindowOpen) {
        return BootstrapStatus::kInvalidState;
    }
    const uint32_t now = clock_.MonotonicMilliseconds();
    if (state_ == BootstrapState::kPairingWindowOpen &&
        DeadlineReached(now, pairing_window_expires_at_ms_)) {
        state_ = BootstrapState::kExpired;
        return BootstrapStatus::kExpired;
    }
    if (IsExpired(now)) {
        state_ = BootstrapState::kExpired;
        return BootstrapStatus::kExpired;
    }
    if (state_ == BootstrapState::kPrepared) {
        pairing_window_expires_at_ms_ = now + config_.pairing_window_ms;
        state_ = BootstrapState::kPairingWindowOpen;
    }
    return BootstrapStatus::kOk;
}

BootstrapStatus PairingBootstrap::BindSession(uint32_t ble_session_id) {
    if (ble_session_id == 0) return BootstrapStatus::kInvalidArgument;
    if (state_ != BootstrapState::kPairingWindowOpen) {
        return BootstrapStatus::kInvalidState;
    }
    const uint32_t now = clock_.MonotonicMilliseconds();
    if (IsExpired(now)) {
        state_ = BootstrapState::kExpired;
        return BootstrapStatus::kExpired;
    }
    if (session_id_ != 0 && session_id_ != ble_session_id) {
        return BootstrapStatus::kSessionMismatch;
    }
    session_id_ = ble_session_id;
    return BootstrapStatus::kOk;
}

BootstrapStatus PairingBootstrap::ValidateEnrollmentProof(
    uint32_t ble_session_id, uint32_t generation, const uint8_t* token,
    std::size_t token_size) {
    if (ble_session_id == 0 || token == nullptr) {
        return BootstrapStatus::kInvalidArgument;
    }
    if (token_size != token_.size()) {
        return BootstrapStatus::kInvalidArgument;
    }
    if (state_ == BootstrapState::kConsumed) {
        return BootstrapStatus::kAlreadyConsumed;
    }
    if (state_ == BootstrapState::kIdle) {
        return BootstrapStatus::kInvalidState;
    }
    if (state_ == BootstrapState::kExpired) {
        return BootstrapStatus::kExpired;
    }
    if (state_ != BootstrapState::kPairingWindowOpen) {
        return BootstrapStatus::kInvalidState;
    }
    const uint32_t now = clock_.MonotonicMilliseconds();
    if (IsExpired(now)) {
        state_ = BootstrapState::kExpired;
        return BootstrapStatus::kExpired;
    }
    if (generation != generation_) {
        return BootstrapStatus::kGenerationMismatch;
    }
    if (!ConstantTimeTokenEqual(token_.data(), token, token_.size())) {
        return BootstrapStatus::kTokenMismatch;
    }
    // Bind only after the proof material is known to be valid. A wrong
    // token or generation must not lock the bootstrap to an attacker's
    // session.
    if (session_id_ != 0 && session_id_ != ble_session_id) {
        return BootstrapStatus::kSessionMismatch;
    }
    session_id_ = ble_session_id;
    token_.fill(0);
    state_ = BootstrapState::kConsumed;
    return BootstrapStatus::kOk;
}

BootstrapStatus PairingBootstrap::Material(BootstrapMaterial* output) const {
    if (output == nullptr) return BootstrapStatus::kInvalidArgument;
    *output = {};
    if (state_ != BootstrapState::kPrepared &&
        state_ != BootstrapState::kPairingWindowOpen) {
        return BootstrapStatus::kInvalidState;
    }
    if (IsExpired(clock_.MonotonicMilliseconds())) {
        return BootstrapStatus::kExpired;
    }
    output->generation = generation_;
    output->token = token_;
    output->active = true;
    return BootstrapStatus::kOk;
}

BootstrapStatus PairingBootstrap::Cancel() {
    token_.fill(0);
    generation_ = 0;
    session_id_ = 0;
    prepared_at_ms_ = 0;
    token_expires_at_ms_ = 0;
    pairing_window_expires_at_ms_ = 0;
    state_ = BootstrapState::kIdle;
    return BootstrapStatus::kOk;
}

bool PairingBootstrap::IsExpired(uint32_t now_ms) const {
    if (state_ == BootstrapState::kPairingWindowOpen) {
        return DeadlineReached(now_ms, token_expires_at_ms_) ||
               DeadlineReached(now_ms, pairing_window_expires_at_ms_);
    }
    return DeadlineReached(now_ms, token_expires_at_ms_);
}

bool PairingBootstrap::DeadlineReached(uint32_t now_ms, uint32_t deadline_ms) {
    // Standard FreeRTOS-style 32-bit monotonic deadline comparison. It is
    // valid for durations below 2^31 ms (enforced by SanitizeDuration) and
    // while callers check deadlines more often than once every 2^31 ms; the
    // bootstrap TTLs make both conditions trivially true.
    return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

bool PairingBootstrap::ConstantTimeTokenEqual(const uint8_t* first,
                                              const uint8_t* second,
                                              std::size_t size) {
    if (first == nullptr || second == nullptr) return false;
    uint8_t difference = 0;
    for (std::size_t index = 0; index < size; ++index) {
        difference |= static_cast<uint8_t>(first[index] ^ second[index]);
    }
    return difference == 0;
}

}  // namespace zectrix::companion
