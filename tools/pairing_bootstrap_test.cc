#include "zectrix_pairing_bootstrap.h"

#include <cassert>
#include <cstdint>
#include <cstring>

namespace {

using zectrix::companion::BootstrapState;
using zectrix::companion::BootstrapStatus;
using zectrix::companion::BootstrapToken;
using zectrix::companion::PairingBootstrap;
using zectrix::companion::PairingBootstrapClock;
using zectrix::companion::PairingBootstrapRandom;

class FakeClock final : public PairingBootstrapClock {
public:
    uint32_t MonotonicMilliseconds() const override { return now_; }
    void Set(uint32_t now_ms) { now_ = now_ms; }
    void Advance(uint32_t delta_ms) { now_ += delta_ms; }

private:
    uint32_t now_ = 0;
};

class FakeRandom final : public PairingBootstrapRandom {
public:
    bool Fill(BootstrapToken* token) override {
        ++calls_;
        if (fail_) return false;
        for (std::size_t i = 0; i < token->size(); ++i) {
            (*token)[i] = static_cast<uint8_t>(0xa0 + calls_ * 7 + i * 3);
        }
        return true;
    }
    void set_fail(bool fail) { fail_ = fail; }
    int calls() const { return calls_; }

private:
    bool fail_ = false;
    int calls_ = 0;
};

void TestPrepareAndMaterial() {
    FakeClock clock;
    FakeRandom random;
    PairingBootstrap bootstrap(clock, random);

    assert(bootstrap.state() == BootstrapState::kIdle);
    assert(bootstrap.Material(nullptr) == BootstrapStatus::kInvalidArgument);

    assert(bootstrap.Prepare() == BootstrapStatus::kOk);
    assert(bootstrap.state() == BootstrapState::kPrepared);
    assert(bootstrap.generation() == 1);

    zectrix::companion::BootstrapMaterial material{};
    assert(bootstrap.Material(&material) == BootstrapStatus::kOk);
    assert(material.active);
    assert(material.generation == 1);
    bool token_nonzero = false;
    for (uint8_t value : material.token) {
        if (value != 0) token_nonzero = true;
    }
    assert(token_nonzero);
}

void TestValidationFlowAndSingleUse() {
    FakeClock clock;
    FakeRandom random;
    PairingBootstrap bootstrap(clock, random);
    assert(bootstrap.Prepare() == BootstrapStatus::kOk);

    zectrix::companion::BootstrapMaterial material{};
    assert(bootstrap.Material(&material) == BootstrapStatus::kOk);
    const uint32_t generation = material.generation;
    const BootstrapToken token = material.token;

    // Field event is required before the proof is accepted.
    assert(bootstrap.ValidateEnrollmentProof(42, generation, token.data(),
                                             token.size()) ==
           BootstrapStatus::kInvalidState);

    assert(bootstrap.OpenPairingWindow() == BootstrapStatus::kOk);
    assert(bootstrap.state() == BootstrapState::kPairingWindowOpen);

    // Session binding is required before a proof can be accepted.
    assert(bootstrap.ValidateEnrollmentProof(42, generation, token.data(),
                                             token.size()) ==
           BootstrapStatus::kSessionMismatch);
    assert(bootstrap.BindSession(0) == BootstrapStatus::kInvalidArgument);
    assert(bootstrap.BindSession(42) == BootstrapStatus::kOk);
    assert(bootstrap.BindSession(43) == BootstrapStatus::kSessionMismatch);

    assert(bootstrap.ValidateEnrollmentProof(0, generation, token.data(),
                                             token.size()) ==
           BootstrapStatus::kInvalidArgument);
    assert(bootstrap.ValidateEnrollmentProof(43, generation, token.data(),
                                             token.size()) ==
           BootstrapStatus::kSessionMismatch);
    assert(bootstrap.ValidateEnrollmentProof(42, generation, nullptr,
                                             token.size()) ==
           BootstrapStatus::kInvalidArgument);
    assert(bootstrap.ValidateEnrollmentProof(42, generation, token.data(),
                                             token.size() - 1) ==
           BootstrapStatus::kInvalidArgument);

    uint8_t wrong_token[16];
    std::memcpy(wrong_token, token.data(), token.size());
    wrong_token[0] ^= 0x01;
    assert(bootstrap.ValidateEnrollmentProof(42, generation, wrong_token,
                                             token.size()) ==
           BootstrapStatus::kTokenMismatch);
    assert(bootstrap.ValidateEnrollmentProof(42, generation + 1, token.data(),
                                             token.size()) ==
           BootstrapStatus::kGenerationMismatch);

    assert(bootstrap.ValidateEnrollmentProof(42, generation, token.data(),
                                             token.size()) ==
           BootstrapStatus::kOk);
    assert(bootstrap.state() == BootstrapState::kConsumed);
    assert(bootstrap.session_id() == 42);

    // Single-use: a second identical proof is rejected.
    assert(bootstrap.ValidateEnrollmentProof(43, generation, token.data(),
                                             token.size()) ==
           BootstrapStatus::kAlreadyConsumed);
    assert(bootstrap.Material(&material) == BootstrapStatus::kInvalidState);
}

void TestExpirationAndPairingWindow() {
    FakeClock clock;
    FakeRandom random;
    zectrix::companion::BootstrapConfig config{};
    config.token_ttl_ms = 5000;
    config.pairing_window_ms = 1000;
    PairingBootstrap bootstrap(clock, random, config);

    assert(bootstrap.Prepare() == BootstrapStatus::kOk);
    zectrix::companion::BootstrapMaterial material{};
    assert(bootstrap.Material(&material) == BootstrapStatus::kOk);

    clock.Advance(5001);
    assert(bootstrap.OpenPairingWindow() == BootstrapStatus::kExpired);
    assert(bootstrap.state() == BootstrapState::kExpired);

    // Pairing-window expiry is independent of token TTL.
    FakeClock clock2;
    FakeRandom random2;
    PairingBootstrap bootstrap2(clock2, random2, config);
    assert(bootstrap2.Prepare() == BootstrapStatus::kOk);
    zectrix::companion::BootstrapMaterial material2{};
    assert(bootstrap2.Material(&material2) == BootstrapStatus::kOk);
    assert(bootstrap2.OpenPairingWindow() == BootstrapStatus::kOk);
    assert(bootstrap2.BindSession(7) == BootstrapStatus::kOk);
    clock2.Advance(1001);
    assert(bootstrap2.ValidateEnrollmentProof(
               7, material2.generation, material2.token.data(),
               material2.token.size()) == BootstrapStatus::kExpired);
}

void TestRebootInvalidation() {
    FakeClock clock;
    FakeRandom random;
    PairingBootstrap first(clock, random);
    assert(first.Prepare() == BootstrapStatus::kOk);
    zectrix::companion::BootstrapMaterial material{};
    assert(first.Material(&material) == BootstrapStatus::kOk);

    // A new instance models a reboot. RAM-only state disappears.
    PairingBootstrap second(clock, random);
    assert(second.state() == BootstrapState::kIdle);
    assert(second.ValidateEnrollmentProof(
               1, material.generation, material.token.data(),
               material.token.size()) == BootstrapStatus::kInvalidState);
}

void TestCancelAndRandomFailure() {
    FakeClock clock;
    FakeRandom random;
    PairingBootstrap bootstrap(clock, random);
    assert(bootstrap.Prepare() == BootstrapStatus::kOk);
    zectrix::companion::BootstrapMaterial material{};
    assert(bootstrap.Material(&material) == BootstrapStatus::kOk);
    assert(material.active);

    assert(bootstrap.Cancel() == BootstrapStatus::kOk);
    assert(bootstrap.state() == BootstrapState::kIdle);
    assert(bootstrap.Material(&material) == BootstrapStatus::kInvalidState);

    random.set_fail(true);
    assert(bootstrap.Prepare() == BootstrapStatus::kInvalidState);
    assert(bootstrap.state() == BootstrapState::kIdle);
    assert(random.calls() == 2);

    // A failed re-prepare must not destroy the currently valid material.
    random.set_fail(false);
    assert(bootstrap.Prepare() == BootstrapStatus::kOk);
    assert(bootstrap.Material(&material) == BootstrapStatus::kOk);
    random.set_fail(true);
    assert(bootstrap.Prepare() == BootstrapStatus::kInvalidState);
    assert(bootstrap.Material(&material) == BootstrapStatus::kOk);
    assert(material.active && material.generation == 1);
    assert(random.calls() == 4);
}

}  // namespace

int main() {
    TestPrepareAndMaterial();
    TestValidationFlowAndSingleUse();
    TestExpirationAndPairingWindow();
    TestRebootInvalidation();
    TestCancelAndRandomFailure();
    return 0;
}
