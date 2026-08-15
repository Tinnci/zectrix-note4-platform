#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace zectrix::companion {

// 128-bit enrollment token. The token is RAM-only, single-use and invalid
// after reboot by construction: no value is persisted.
using BootstrapToken = std::array<uint8_t, 16>;

enum class BootstrapState : uint8_t {
    kIdle = 0,
    kPrepared,
    kPairingWindowOpen,
    kExpired,
    kConsumed,
};

enum class BootstrapStatus : uint8_t {
    kOk = 0,
    kInvalidArgument,
    kInvalidState,
    kTokenMismatch,
    kGenerationMismatch,
    kSessionMismatch,
    kExpired,
    kAlreadyConsumed,
};

class PairingBootstrapClock {
public:
    virtual ~PairingBootstrapClock() = default;
    virtual uint32_t MonotonicMilliseconds() const = 0;
};

class PairingBootstrapRandom {
public:
    virtual ~PairingBootstrapRandom() = default;
    // Returns false when no cryptographically safe random source is
    // available. The bootstrap must not produce a predictable token.
    virtual bool Fill(BootstrapToken* token) = 0;
};

struct BootstrapConfig {
    uint32_t token_ttl_ms = 120000;
    uint32_t pairing_window_ms = 120000;
};

struct BootstrapMaterial {
    uint32_t generation = 0;
    BootstrapToken token{};
    bool active = false;
};

class PairingBootstrap {
public:
    PairingBootstrap(PairingBootstrapClock& clock,
                     PairingBootstrapRandom& random,
                     BootstrapConfig config = {});

    PairingBootstrap(const PairingBootstrap&) = delete;
    PairingBootstrap& operator=(const PairingBootstrap&) = delete;

    BootstrapState state() const { return state_; }
    uint32_t generation() const { return generation_; }
    uint32_t session_id() const { return session_id_; }
    uint32_t pairing_window_ms() const { return config_.pairing_window_ms; }

    // Generates a fresh token and advances the generation. The previous
    // material becomes invalid immediately. State moves to kPrepared.
    BootstrapStatus Prepare();

    // Opens the local BLE pairing window after an NFC field-rising event.
    // Rejects an expired token. The caller is responsible for forwarding the
    // resulting window duration to the BLE transport owner.
    BootstrapStatus OpenPairingWindow();

    // Binds the bootstrap to the single BLE session that will carry the
    // enrollment proof. A later bind to a different session is rejected with
    // kSessionMismatch; a late proof from a different session is rejected too.
    BootstrapStatus BindSession(uint32_t ble_session_id);

    // Consumes a token returned through the authenticated protocol Hello.
    // The proof must arrive on the session bound by BindSession(). On success
    // the bootstrap zeroizes the token and moves to kConsumed. It is
    // single-use: a second valid proof must be rejected.
    BootstrapStatus ValidateEnrollmentProof(uint32_t ble_session_id,
                                            uint32_t generation,
                                            const uint8_t* token,
                                            std::size_t token_size);

    // Copies the current material for NDEF preparation. Only available while
    // kPrepared or kPairingWindowOpen. The token is zeroized on consumption,
    // cancellation or destruction.
    BootstrapStatus Material(BootstrapMaterial* output) const;

    // Discards the current material and returns to kIdle.
    BootstrapStatus Cancel();

private:
    bool IsExpired(uint32_t now_ms) const;
    static bool ConstantTimeTokenEqual(const uint8_t* first,
                                       const uint8_t* second,
                                       std::size_t size);

    PairingBootstrapClock& clock_;
    PairingBootstrapRandom& random_;
    BootstrapConfig config_;
    BootstrapState state_ = BootstrapState::kIdle;
    BootstrapToken token_{};
    uint32_t generation_ = 0;
    uint32_t session_id_ = 0;
    uint32_t prepared_at_ms_ = 0;
    uint32_t token_expires_at_ms_ = 0;
    uint32_t pairing_window_expires_at_ms_ = 0;
};

}  // namespace zectrix::companion
