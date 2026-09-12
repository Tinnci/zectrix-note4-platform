#include "zectrix_enrollment_publisher.h"

#include <algorithm>

namespace zectrix::companion {
namespace {

uint32_t Remaining(uint32_t now, uint32_t deadline) {
    return static_cast<int32_t>(deadline - now) > 0 ? deadline - now : 0;
}

void ClearMaterial(BootstrapMaterial& material) {
    volatile uint8_t* bytes = material.token.data();
    for (std::size_t i = 0; i < material.token.size(); ++i) bytes[i] = 0;
}

}  // namespace

bool EnrollmentPublisher::Refresh(
    PairingBootstrap& bootstrap, uint32_t now_ms, bool field_present,
    const std::function<bool(const BootstrapMaterial&)>& write) {
    if (field_present || !write ||
        (retry_pending_ && Remaining(now_ms, retry_at_ms_) != 0)) return false;

    BootstrapMaterial material{};
    if (published_generation_ != 0 && published_generation_ == bootstrap.generation() &&
        bootstrap.Material(&material) == BootstrapStatus::kOk) {
        ClearMaterial(material);
        return false;
    }

    published_generation_ = 0;
    const bool published = bootstrap.Prepare() == BootstrapStatus::kOk &&
        bootstrap.Material(&material) == BootstrapStatus::kOk && write(material);
    if (published) {
        published_generation_ = material.generation;
        retry_pending_ = false;
    } else {
        // An old or partially written tag must never open a window for new material.
        bootstrap.Cancel();
        retry_pending_ = true;
        retry_at_ms_ = now_ms + 1000;
    }
    ClearMaterial(material);
    return published;
}

BootstrapStatus EnrollmentPublisher::OpenPairingWindow(PairingBootstrap& bootstrap) const {
    if (published_generation_ == 0 || published_generation_ != bootstrap.generation()) {
        return BootstrapStatus::kInvalidState;
    }
    return bootstrap.OpenPairingWindow();
}

uint32_t EnrollmentPublisher::NextWakeMs(
    const PairingBootstrap& bootstrap, uint32_t now_ms, bool field_present) const {
    // Publication cannot progress in an RF field. Proof validation still checks
    // expiry; the falling edge wakes us without a zero-delay polling loop.
    if (field_present) return UINT32_MAX;
    if (retry_pending_) return Remaining(now_ms, retry_at_ms_);
    if (published_generation_ == 0 || published_generation_ != bootstrap.generation()) return 0;
    switch (bootstrap.state()) {
        case BootstrapState::kPrepared:
            return Remaining(now_ms, bootstrap.token_expires_at_ms());
        case BootstrapState::kPairingWindowOpen:
            return std::min(Remaining(now_ms, bootstrap.token_expires_at_ms()),
                            Remaining(now_ms, bootstrap.pairing_window_expires_at_ms()));
        default:
            return 0;
    }
}

void EnrollmentPublisher::Reset() {
    published_generation_ = 0;
    retry_at_ms_ = 0;
    retry_pending_ = false;
}

}  // namespace zectrix::companion
