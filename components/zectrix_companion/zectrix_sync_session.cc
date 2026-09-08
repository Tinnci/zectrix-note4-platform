#include "zectrix_sync_session.h"

#include <algorithm>
#include <cstring>

namespace zectrix::companion {
namespace {
constexpr uint32_t kRetryMs = 3000;
constexpr uint32_t kProgressTimeoutMs = 15000;
constexpr unsigned kMaximumAttempts = 3;
constexpr uint16_t kKey = 1;
constexpr uint16_t kRevision = 2;
constexpr uint16_t kValue = 3;
constexpr uint16_t kResult = 4;

uint32_t Read(const uint8_t* p, unsigned bytes) {
    uint32_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) value |= static_cast<uint32_t>(p[i]) << (8 * i);
    return value;
}
void Write(uint8_t* p, uint32_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) p[i] = static_cast<uint8_t>(value >> (8 * i));
}
uint32_t Remaining(uint32_t now, uint32_t deadline) {
    return static_cast<int32_t>(deadline - now) > 0 ? deadline - now : 0;
}

bool DecodeState(const FrameView& frame, bool reply, DurableStateView* state, SyncReply* result) {
    TlvReader reader(frame.payload, frame.payload_size);
    TlvField field{};
    bool present = false;
    unsigned seen = 0;
    while (reader.Next(&field, &present) == ProtocolStatus::kOk) {
        if (!present) return seen == 7 && state->key != 0 && state->revision != 0;
        unsigned bit = 0;
        if (field.type == kKey && field.value_size == 2) {
            bit = 1;
            state->key = static_cast<uint16_t>(Read(field.value, 2));
        } else if (field.type == kRevision && field.value_size == 4) {
            bit = 2;
            state->revision = Read(field.value, 4);
        } else if (!reply && field.type == kValue && field.value_size <= kDurableValueCapacity) {
            bit = 4;
            state->value = field.value;
            state->value_size = field.value_size;
        } else if (reply && field.type == kResult && field.value_size == 1 &&
                   field.value[0] <= static_cast<uint8_t>(SyncReply::kCapacity)) {
            bit = 4;
            *result = static_cast<SyncReply>(field.value[0]);
        } else if (field.required || (field.type >= kKey && field.type <= kResult)) {
            return false;
        }
        if ((seen & bit) != 0) return false;
        seen |= bit;
    }
    return false;
}

std::size_t EncodePayload(const DurableStateView& state, const SyncReply* result,
                           uint8_t* output, std::size_t capacity) {
    TlvWriter writer(output, capacity);
    uint8_t key[2];
    Write(key, state.key, 2);
    if (writer.Add(kRequiredFieldBit | kKey, key, 2) != ProtocolStatus::kOk ||
        writer.AddUInt32(kRequiredFieldBit | kRevision, state.revision) != ProtocolStatus::kOk) return 0;
    const uint8_t code = result == nullptr ? 0 : static_cast<uint8_t>(*result);
    if (writer.Add(kRequiredFieldBit | (result == nullptr ? kValue : kResult),
                   result == nullptr ? state.value : &code,
                   result == nullptr ? state.value_size : 1) != ProtocolStatus::kOk) return 0;
    return writer.Size();
}
}  // namespace

ProtocolStatus EncodeSyncCursors(const SyncCursors& cursors, uint8_t* output,
                                 std::size_t capacity, std::size_t* size) {
    if (output == nullptr || size == nullptr || cursors.count > kDurableKeyCapacity) return ProtocolStatus::kInvalidArgument;
    *size = 0;
    if (capacity < 4 + 14 * cursors.count) return ProtocolStatus::kBufferTooSmall;
    output[0] = 1;
    output[1] = static_cast<uint8_t>(cursors.count);
    output[2] = output[3] = 0;
    for (std::size_t i = 0; i < cursors.count; ++i) {
        const auto& entry = cursors.entries[i];
        auto* p = output + 4 + 14 * i;
        Write(p, entry.key, 2);
        Write(p + 2, entry.outbound_acknowledged, 4);
        Write(p + 6, entry.inbound_applied, 4);
        Write(p + 10, entry.pending_revision, 4);
    }
    SyncCursors validated{};
    const auto status = DecodeSyncCursors(output, 4 + 14 * cursors.count, &validated);
    if (status == ProtocolStatus::kOk) *size = 4 + 14 * cursors.count;
    return status;
}

ProtocolStatus DecodeSyncCursors(const uint8_t* input, std::size_t size, SyncCursors* cursors) {
    if (input == nullptr || cursors == nullptr) return ProtocolStatus::kInvalidArgument;
    *cursors = {};
    if (size < 4 || input[0] != 1 || input[1] > kDurableKeyCapacity ||
        input[2] != 0 || input[3] != 0 || size != 4 + 14U * input[1]) return ProtocolStatus::kMalformedTlv;
    SyncCursors decoded{};
    decoded.count = input[1];
    for (std::size_t i = 0; i < decoded.count; ++i) {
        const auto* p = input + 4 + 14 * i;
        auto& entry = decoded.entries[i];
        entry = {static_cast<uint16_t>(Read(p, 2)), Read(p + 2, 4), Read(p + 6, 4), Read(p + 10, 4)};
        if (entry.key == 0 || (entry.pending_revision != 0 && entry.pending_revision <= entry.outbound_acknowledged)) return ProtocolStatus::kMalformedTlv;
        for (std::size_t j = 0; j < i; ++j) {
            if (decoded.entries[j].key == entry.key) return ProtocolStatus::kMalformedTlv;
        }
    }
    *cursors = decoded;
    return ProtocolStatus::kOk;
}

SyncStatus SyncSession::Start(const SyncCursors& peer, uint32_t now_ms, uint32_t last_incoming_sequence) {
    Disconnect();
    const auto status = engine_.ReconcilePeerCursors(peer);
    if (status != SyncStatus::kOk) return status;
    peer_ = peer;
    status_ = SyncSessionStatus::kActive;
    last_incoming_sequence_ = last_incoming_sequence;
    progress_deadline_ = now_ms + kProgressTimeoutMs;
    return SyncStatus::kOk;
}

void SyncSession::Disconnect() {
    status_ = SyncSessionStatus::kDisconnected;
    idle_ = false;
    progress_pending_ = false;
    peer_ = {};
    outbound_size_ = reply_size_ = last_payload_size_ = 0;
    outbound_sequence_ = last_incoming_sequence_ = 0;
    attempts_ = 0;
    after_reply_ = SyncSessionStatus::kActive;
}

bool SyncSession::Receive(const FrameView& frame) {
    if (status_ != SyncSessionStatus::kActive) return false;
    const auto& h = frame.header;
    const bool reply = h.message_class == MessageClass::kControl && h.message_type == kDurableAckMessageType;
    const bool state_frame = h.message_class == MessageClass::kDurableState && h.message_type == kDurableStateMessageType;
    if (!reply && !state_frame) return false;
    DurableStateView state{};
    SyncReply result{};
    if (h.flags != (reply ? kResponse : kAckRequested | kRetriable) ||
        h.sequence == 0 || h.request_id != h.sequence ||
        frame.payload_size > last_payload_.size() || !DecodeState(frame, reply, &state, &result)) {
        status_ = SyncSessionStatus::kProtocolError;
        return true;
    }
    if (reply) {
        // Stale or unsolicited ACKs cannot retire a different in-flight value.
        if (outbound_size_ == 0 || attempts_ == 0 || h.sequence != outbound_sequence_ ||
            state.key != outbound_key_ || state.revision != outbound_revision_) return true;
        if (result != SyncReply::kAccepted) {
            status_ = result == SyncReply::kStoreError ? SyncSessionStatus::kStoreError : SyncSessionStatus::kProtocolError;
            return true;
        }
        const auto status = engine_.AcknowledgeDurableState(state.key, state.revision);
        if (status != SyncStatus::kOk && status != SyncStatus::kDuplicate) {
            status_ = SyncSessionStatus::kStoreError;
        } else {
            outbound_size_ = 0;
            progress_pending_ = true;
        }
        return true;
    }
    if (h.sequence < last_incoming_sequence_ ||
        (h.sequence == last_incoming_sequence_ && (frame.payload_size != last_payload_size_ ||
          std::memcmp(frame.payload, last_payload_.data(), frame.payload_size) != 0))) {
        status_ = SyncSessionStatus::kProtocolError;
        return true;
    }
    if (reply_size_ != 0 && h.sequence != last_incoming_sequence_) {
        status_ = SyncSessionStatus::kProtocolError;
        return true;
    }
    // Keep the original reply while TX is busy, including a failed save's NACK.
    if (reply_size_ != 0) return true;
    const auto accepted = engine_.AcceptIncomingState(state);
    if (accepted == SyncStatus::kOk) progress_pending_ = true;
    result = accepted == SyncStatus::kOk || accepted == SyncStatus::kDuplicate ? SyncReply::kAccepted :
        accepted == SyncStatus::kStoreError ? SyncReply::kStoreError :
        accepted == SyncStatus::kOutboxFull ? SyncReply::kCapacity : SyncReply::kInvalid;
    after_reply_ = result == SyncReply::kAccepted ? SyncSessionStatus::kActive :
        result == SyncReply::kStoreError ? SyncSessionStatus::kStoreError : SyncSessionStatus::kProtocolError;
    last_incoming_sequence_ = h.sequence;
    last_payload_size_ = frame.payload_size;
    std::memcpy(last_payload_.data(), frame.payload, frame.payload_size);
    uint8_t payload[19];
    const auto size = EncodePayload(state, &result, payload, sizeof(payload));
    FrameHeader header{};
    header.flags = kResponse;
    header.message_type = kDurableAckMessageType;
    header.sequence = header.request_id = h.sequence;
    EncodeFrame(header, payload, size, reply_.data(), reply_.size(), &reply_size_);
    return true;
}

void SyncSession::Poll(SyncFrameSender& sender, uint32_t& next_sequence,
                       uint32_t now_ms, bool allow_new_state) {
    if (status_ != SyncSessionStatus::kActive) return;
    if (Converged()) {
        idle_ = true;
        return;
    }
    // A resource request already owns this direction's confirmation window.
    if (!allow_new_state && outbound_size_ == 0 && reply_size_ == 0) {
        idle_ = true;
        return;
    }
    // Duplicate traffic is not progress and cannot keep a stalled replay alive.
    if (idle_ || progress_pending_) progress_deadline_ = now_ms + kProgressTimeoutMs;
    idle_ = false;
    progress_pending_ = false;
    if (Remaining(now_ms, progress_deadline_) == 0) { status_ = SyncSessionStatus::kTimeout; return; }
    if (reply_size_ != 0) {
        if (sender.SendSyncFrame(reply_.data(), reply_size_) == LinkResult::kOk) {
            reply_size_ = 0;
            status_ = after_reply_;
        }
        return;
    }
    if (outbound_size_ == 0 && allow_new_state) {
        DurableStateView state{};
        if (engine_.NextDurableState(&state) != SyncStatus::kOk) return;
        if (next_sequence == 0 || next_sequence == UINT32_MAX) { status_ = SyncSessionStatus::kProtocolError; return; }
        uint8_t payload[kSyncFrameSize - kFrameHeaderSize];
        const auto size = EncodePayload(state, nullptr, payload, sizeof(payload));
        FrameHeader header{};
        header.message_class = MessageClass::kDurableState;
        header.flags = kAckRequested | kRetriable;
        header.message_type = kDurableStateMessageType;
        header.sequence = header.request_id = next_sequence++;
        EncodeFrame(header, payload, size, outbound_.data(), outbound_.size(), &outbound_size_);
        outbound_key_ = state.key;
        outbound_revision_ = state.revision;
        outbound_sequence_ = header.sequence;
        attempts_ = 0;
        retry_at_ = now_ms;
        progress_deadline_ = now_ms + kProgressTimeoutMs;
    }
    if (outbound_size_ == 0 || Remaining(now_ms, retry_at_) != 0) return;
    if (attempts_ == kMaximumAttempts) { status_ = SyncSessionStatus::kTimeout; return; }
    if (sender.SendSyncFrame(outbound_.data(), outbound_size_) == LinkResult::kOk) {
        ++attempts_;
        retry_at_ = now_ms + kRetryMs;
    }
}

bool SyncSession::Converged() const {
    if (status_ != SyncSessionStatus::kActive || outbound_size_ != 0 || reply_size_ != 0 ||
        engine_.PendingDurableCount() != 0) return false;
    for (std::size_t i = 0; i < peer_.count; ++i) {
        const auto& entry = peer_.entries[i];
        if (entry.pending_revision != 0 &&
            engine_.InspectIncomingState(entry.key, entry.pending_revision) != SyncStatus::kDuplicate) return false;
    }
    return true;
}

uint32_t SyncSession::NextWakeMs(uint32_t now_ms, bool allow_new_state) const {
    if (status_ != SyncSessionStatus::kActive || Converged()) return UINT32_MAX;
    if (!allow_new_state && outbound_size_ == 0 && reply_size_ == 0) return UINT32_MAX;
    if (reply_size_ != 0 || (outbound_size_ == 0 && engine_.PendingDurableCount() != 0)) return 20;
    const auto deadline = Remaining(now_ms, progress_deadline_);
    return outbound_size_ != 0 ? std::min(deadline, std::max<uint32_t>(20, Remaining(now_ms, retry_at_))) : deadline;
}

}  // namespace zectrix::companion
