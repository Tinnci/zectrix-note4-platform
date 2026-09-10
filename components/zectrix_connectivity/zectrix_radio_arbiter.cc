#include "zectrix_radio_arbiter.h"

#include <algorithm>

namespace zectrix::connectivity {

void RadioArbiter::Update(WifiBackendState resource, const BookTransferSnapshot& books,
                          bool wifi_claimed, uint32_t now_ms) {
    RadioMode next = RadioMode::kCompanion;
    if (resource == WifiBackendState::kStopping || resource == WifiBackendState::kStopFailed ||
        books.state == BookTransferState::Stopping) {
        next = RadioMode::kWifiStopping;
    } else if (resource != WifiBackendState::kStopped || books.state == BookTransferState::Starting) {
        next = RadioMode::kWifiBurst;
    } else if (books.state == BookTransferState::Sharing) {
        // HTTP can publish activity just after the owner captures now_ms.
        const uint32_t elapsed = now_ms - books.activity_ms;
        const bool recent = (elapsed & 0x80000000U) != 0 || elapsed < kWifiQuietMs;
        next = books.client_active || recent ? RadioMode::kWifiBurst : RadioMode::kSharedIdle;
    } else if (wifi_claimed) {
        // Diagnostics share the driver claim but run outside this session owner.
        next = RadioMode::kWifiBurst;
    }
    const bool was_pacing = Pacing();
    mode_ = next;
    // Allow startup a quiet interval. Burst/cleanup transitions cannot postpone
    // the next admission indefinitely, even when HTTP activity keeps arriving.
    if (Pacing() && !was_pacing) last_sync_ms_ = now_ms;
}

bool RadioArbiter::Pacing() const {
    return mode_ == RadioMode::kWifiBurst || mode_ == RadioMode::kWifiStopping;
}

uint32_t RadioArbiter::SyncDelayMs(uint32_t now_ms) const {
    const uint32_t elapsed = now_ms - last_sync_ms_;
    return Pacing() && elapsed < kSyncIntervalMs ? kSyncIntervalMs - elapsed : 0;
}

void RadioArbiter::PollSync(companion::SyncSession& session, companion::SyncFrameSender& sender,
                            uint32_t& next_sequence, uint32_t now_ms, bool awaiting_phone) {
    // ACK/NACK, an admitted frame and its retries bypass the admission delay.
    // Pairing and Hello are handled independently by the existing BLE owner.
    if (session.Poll(sender, next_sequence, now_ms, !awaiting_phone && SyncDelayMs(now_ms) == 0)) {
        last_sync_ms_ = now_ms;
    }
}

uint32_t RadioArbiter::NextSyncWakeMs(const companion::SyncSession& session, uint32_t now_ms,
                                    bool awaiting_phone) const {
    const uint32_t delay = SyncDelayMs(now_ms);
    uint32_t next = session.NextWakeMs(now_ms, !awaiting_phone && delay == 0);
    if (!awaiting_phone && delay != 0 && session.Status() == companion::SyncSessionStatus::kActive &&
        !session.Converged()) {
        next = std::min(next, delay);
    }
    return next;
}

}  // namespace zectrix::connectivity
