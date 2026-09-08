#pragma once

#include "zectrix_companion_protocol.h"
#include "zectrix_connectivity_policy.h"
#include "zectrix_sync_engine.h"

namespace zectrix::companion {

constexpr uint16_t kHelloSyncCursorsType = 4;
constexpr uint16_t kDurableStateMessageType = 1;
constexpr uint16_t kDurableAckMessageType = 3;
constexpr std::size_t kSyncCursorValueSize = 4 + 14 * kDurableKeyCapacity;
constexpr std::size_t kSyncFrameSize = kFrameHeaderSize + 18 + kDurableValueCapacity;

ProtocolStatus EncodeSyncCursors(const SyncCursors& cursors, uint8_t* output,
                                 std::size_t capacity, std::size_t* size);
ProtocolStatus DecodeSyncCursors(const uint8_t* input, std::size_t size,
                                 SyncCursors* cursors);

enum class SyncReply : uint8_t { kAccepted = 0, kStoreError = 1, kInvalid = 2, kCapacity = 3 };
enum class SyncSessionStatus : uint8_t { kDisconnected, kActive, kProtocolError, kStoreError, kTimeout };

class SyncFrameSender {
public:
    virtual ~SyncFrameSender() = default;
    virtual LinkResult SendSyncFrame(const uint8_t* frame, std::size_t size) = 0;
};

// The connectivity owner serializes this state with the engine. Disconnect
// clears only transport state; the durable store always owns pending values.
class SyncSession {
public:
    explicit SyncSession(SyncEngine& engine) : engine_(engine) {}
    SyncStatus Start(const SyncCursors& peer, uint32_t now_ms,
                     uint32_t last_incoming_sequence = 0);
    void Disconnect();
    bool Receive(const FrameView& frame);
    void Poll(SyncFrameSender& sender, uint32_t& next_sequence,
              uint32_t now_ms, bool allow_new_state = true);
    uint32_t NextWakeMs(uint32_t now_ms, bool allow_new_state = true) const;
    bool Converged() const;
    SyncSessionStatus Status() const { return status_; }

private:
    SyncEngine& engine_;
    SyncCursors peer_{};
    SyncSessionStatus status_ = SyncSessionStatus::kDisconnected;
    bool idle_ = false;
    std::array<uint8_t, kSyncFrameSize> outbound_{};
    std::size_t outbound_size_ = 0;
    uint32_t outbound_sequence_ = 0;
    uint16_t outbound_key_ = 0;
    uint32_t outbound_revision_ = 0;
    uint32_t retry_at_ = 0;
    uint32_t progress_deadline_ = 0;
    unsigned attempts_ = 0;
    std::array<uint8_t, kFrameHeaderSize + 19> reply_{};
    std::size_t reply_size_ = 0;
    SyncSessionStatus after_reply_ = SyncSessionStatus::kActive;
    uint32_t last_incoming_sequence_ = 0;
    std::array<uint8_t, kSyncFrameSize - kFrameHeaderSize> last_payload_{};
    std::size_t last_payload_size_ = 0;
};

}  // namespace zectrix::companion
