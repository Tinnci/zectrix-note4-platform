#pragma once

#include "zectrix_book_transfer.h"
#include "zectrix_sync_session.h"

namespace zectrix::connectivity {

enum class RadioMode : uint8_t { kCompanion, kWifiBurst, kSharedIdle, kWifiStopping };

// The Connectivity owner serializes this scheduler with its existing mutex.
// ESP-IDF still owns antenna time slices, link control and modem sleep.
class RadioArbiter {
public:
    static constexpr uint32_t kSyncIntervalMs = 250;
    static constexpr uint32_t kWifiQuietMs = 500;

    void Update(WifiBackendState resource, const BookTransferSnapshot& books,
                bool wifi_claimed, uint32_t now_ms);
    void PollSync(companion::SyncSession& session, companion::SyncFrameSender& sender,
                  uint32_t& next_sequence, uint32_t now_ms, bool awaiting_phone);
    uint32_t NextSyncWakeMs(const companion::SyncSession& session, uint32_t now_ms,
                           bool awaiting_phone) const;
    RadioMode Mode() const { return mode_; }

private:
    bool Pacing() const;
    uint32_t SyncDelayMs(uint32_t now_ms) const;
    RadioMode mode_ = RadioMode::kCompanion;
    uint32_t last_sync_ms_ = 0;
};

}  // namespace zectrix::connectivity
