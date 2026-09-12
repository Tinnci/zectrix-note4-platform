#pragma once

#include "zectrix_pairing_bootstrap.h"

namespace zectrix::companion {

// Runs on the existing session owner. RF callbacks only wake that owner.
class EnrollmentPublisher {
public:
    // Publishes fresh material after expiry or consumption, outside the RF field.
    // A failed write invalidates the proof and is retried after one second.
    bool Refresh(PairingBootstrap& bootstrap, uint32_t now_ms, bool field_present,
                 const std::function<bool(const BootstrapMaterial&)>& write);
    BootstrapStatus OpenPairingWindow(PairingBootstrap& bootstrap) const;
    uint32_t NextWakeMs(const PairingBootstrap& bootstrap, uint32_t now_ms,
                        bool field_present) const;
    void Reset();

private:
    uint32_t published_generation_ = 0;
    uint32_t retry_at_ms_ = 0;
    bool retry_pending_ = false;
};

}  // namespace zectrix::companion
