#pragma once

#include "zectrix_companion_protocol.h"

namespace zectrix::companion {

// One copied hint bridges the BLE/session task and foreground time owner.
// The caller serializes access and supplies the currently authorized session.
class ClockMailbox {
public:
    static constexpr uint64_t kMaximumAgeMs = 30000;

    void Offer(const ClockSample& sample, uint32_t session_id, uint64_t received_at_ms) {
        sample_ = sample;
        session_id_ = session_id;
        received_at_ms_ = received_at_ms;
    }
    void Clear() { session_id_ = 0; }
    bool Take(uint32_t authorized_session_id, uint64_t now_ms, ClockSample* output) {
        if (!output) return false;
        const uint32_t session_id = session_id_;
        Clear();
        if (!session_id || session_id != authorized_session_id || now_ms < received_at_ms_ ||
            now_ms - received_at_ms_ > kMaximumAgeMs || sample_.unix_milliseconds < 0 ||
            sample_.unix_milliseconds > INT64_MAX - static_cast<int64_t>(now_ms - received_at_ms_)) return false;
        *output = sample_;
        output->unix_milliseconds += static_cast<int64_t>(now_ms - received_at_ms_);
        return true;
    }

private:
    ClockSample sample_{};
    uint32_t session_id_ = 0;
    uint64_t received_at_ms_ = 0;
};

}  // namespace zectrix::companion
