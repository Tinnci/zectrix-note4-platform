#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "zectrix_sync_engine.h"
#include "zectrix_sync_session.h"

using namespace zectrix::companion;

namespace {

class MemoryStore final : public SyncStore {
public:
    StoreReadStatus Load(uint8_t* output, std::size_t capacity,
                         std::size_t* output_size) override {
        if (read_error) return StoreReadStatus::kError;
        if (bytes.empty()) return StoreReadStatus::kNotFound;
        if (output == nullptr || output_size == nullptr ||
            capacity < bytes.size()) {
            return StoreReadStatus::kError;
        }
        std::memcpy(output, bytes.data(), bytes.size());
        *output_size = bytes.size();
        return StoreReadStatus::kOk;
    }

    bool Save(const uint8_t* input, std::size_t size) override {
        ++saves;
        if (fail_next_save) {
            fail_next_save = false;
            return false;
        }
        bytes.assign(input, input + size);
        return true;
    }

    std::vector<uint8_t> bytes;
    bool read_error = false;
    bool fail_next_save = false;
    unsigned saves = 0;
};

class Sender final : public SyncFrameSender {
public:
    LinkResult SendSyncFrame(const uint8_t* data, std::size_t size) override {
        if (busy) return LinkResult::kBusy;
        frame.assign(data, data + size);
        ++sends;
        return LinkResult::kOk;
    }
    std::vector<uint8_t> frame;
    bool busy = false;
    unsigned sends = 0;
};

void Receive(SyncSession& session, const std::vector<uint8_t>& bytes) {
    FrameView frame{};
    assert(DecodeFrame(bytes.data(), bytes.size(), kProtocolMajor, kProtocolMinor, &frame) == ProtocolStatus::kOk);
    assert(session.Receive(frame));
}

void Transfer(SyncSession& from, SyncSession& to, uint32_t& sequence, uint32_t now = 0) {
    Sender sender;
    from.Poll(sender, sequence, now);
    if (!sender.frame.empty()) Receive(to, sender.frame);
}

void TestBidirectionalReconnectWithLostAck() {
    MemoryStore device_store, phone_store;
    const uint8_t value[] = {9, 8, 7};
    {
        SyncEngine device, phone;
        assert(device.Initialize(device_store) == SyncStatus::kOk);
        assert(phone.Initialize(phone_store) == SyncStatus::kOk);
        assert(device.PutDurableState(1, 3, value, sizeof(value)) == SyncStatus::kOk);
        assert(phone.PutDurableState(2, 8, value, sizeof(value)) == SyncStatus::kOk);
        const auto dc = device.Cursors(), pc = phone.Cursors();
        SyncSession ds(device), ps(phone);
        assert(ds.Start(pc, 0) == SyncStatus::kOk);
        assert(ps.Start(dc, 0) == SyncStatus::kOk);
        uint32_t sequence = 1;
        Transfer(ds, ps, sequence);
        assert(device.PendingDurableCount() == 1);
        DurableStateView received{};
        assert(phone.ReadIncomingState(1, &received) == SyncStatus::kOk);
        assert(received.revision == 3 && received.value_size == sizeof(value));
        // The link disappears after durable receipt, before the ACK is sent.
        ds.Disconnect();
        ps.Disconnect();
        assert(device.PendingDurableCount() == 1);
    }
    SyncEngine device, phone;
    assert(device.Initialize(device_store) == SyncStatus::kOk);
    assert(phone.Initialize(phone_store) == SyncStatus::kOk);
    const auto dc = device.Cursors(), pc = phone.Cursors();
    SyncSession ds(device), ps(phone);
    assert(ds.Start(pc, 100) == SyncStatus::kOk);
    assert(ps.Start(dc, 100) == SyncStatus::kOk);
    assert(device.PendingDurableCount() == 0);
    assert(!ds.Converged() && !ps.Converged());
    uint32_t device_sequence = 1, phone_sequence = 2;
    for (unsigned i = 0; i < 4; ++i) {
        Transfer(ps, ds, phone_sequence, 100);
        Transfer(ds, ps, device_sequence, 100);
    }
    assert(ds.Converged() && ps.Converged());
    assert(device.Cursors().entries[1].inbound_applied == 8);
    // Idle time does not consume the deadline for a later mutation.
    Sender sender;
    ds.Poll(sender, device_sequence, 100);
    assert(device.PutDurableState(1, 4, value, sizeof(value)) == SyncStatus::kOk);
    ds.Poll(sender, device_sequence, 100000);
    assert(ds.Status() == SyncSessionStatus::kActive && sender.sends == 1);
}

void TestInFlightReplacementAndExactAck() {
    MemoryStore a_store, b_store;
    SyncEngine a, b;
    assert(a.Initialize(a_store) == SyncStatus::kOk);
    assert(b.Initialize(b_store) == SyncStatus::kOk);
    const uint8_t value = 5;
    assert(a.PutDurableState(1, 1, &value, 1) == SyncStatus::kOk);
    SyncSession as(a), bs(b);
    const auto ac = a.Cursors();
    assert(as.Start(b.Cursors(), 0) == SyncStatus::kOk);
    assert(bs.Start(ac, 0) == SyncStatus::kOk);
    Sender outbound, reply;
    uint32_t aseq = 1, bseq = 1;
    as.Poll(outbound, aseq, 0);
    assert(a.PutDurableState(1, 2, &value, 1) == SyncStatus::kOk);
    // A timeout replays the original frame, even after coalescing the outbox.
    const auto original = outbound.frame;
    as.Poll(outbound, aseq, 3000);
    assert(outbound.frame == original);
    Receive(bs, outbound.frame);
    const auto saves = b_store.saves;
    Receive(bs, outbound.frame);
    assert(b_store.saves == saves);
    bs.Poll(reply, bseq, 3000);
    const auto old_ack = reply.frame;
    Receive(as, old_ack);
    assert(a.PendingDurableCount() == 1);
    assert(a.Cursors().entries[0].outbound_acknowledged == 1);
    as.Poll(outbound, aseq, 3000);
    Receive(as, old_ack);
    assert(a.PendingDurableCount() == 1);
    Receive(bs, outbound.frame);
    bs.Poll(reply, bseq, 3000);
    Receive(as, reply.frame);
    assert(a.PendingDurableCount() == 0 && as.Converged());
}

void TestPersistenceRollbackAndPeerRegression() {
    MemoryStore store;
    SyncEngine engine;
    assert(engine.Initialize(store) == SyncStatus::kOk);
    for (uint16_t key = 1; key <= 10; ++key) {
        store.fail_next_save = true;
        assert(engine.CommitIncomingState(key, 1) == SyncStatus::kStoreError);
        assert(engine.Cursors().count == 0);
    }
    const uint8_t value = 1;
    store.fail_next_save = true;
    assert(engine.AcceptIncomingState({1, 1, &value, 1}) == SyncStatus::kStoreError);
    assert(engine.Cursors().count == 0);
    DurableStateView state{};
    assert(engine.ReadIncomingState(1, &state) == SyncStatus::kNotFound);
    assert(engine.PutDurableState(1, 3, &value, 1) == SyncStatus::kOk);
    const auto persisted = store.bytes;
    SyncCursors peer{};
    peer.count = 1;
    peer.entries[0] = {1, 0, 3, 0};
    store.fail_next_save = true;
    assert(engine.ReconcilePeerCursors(peer) == SyncStatus::kStoreError);
    assert(store.bytes == persisted && engine.PendingDurableCount() == 1);
    assert(engine.Cursors().entries[0].outbound_acknowledged == 0);
    store.fail_next_save = true;
    assert(engine.AcknowledgeDurableState(1, 3) == SyncStatus::kStoreError);
    assert(engine.PendingDurableCount() == 1);
    assert(engine.ReconcilePeerCursors(peer) == SyncStatus::kOk);
    peer.entries[0].inbound_applied = 2;
    assert(engine.ReconcilePeerCursors(peer) == SyncStatus::kResyncRequired);
    peer.entries[0].inbound_applied = 4;
    assert(engine.ReconcilePeerCursors(peer) == SyncStatus::kResyncRequired);
}

void TestBoundedRetriesAndReceiveSaveFailure() {
    MemoryStore a_store, b_store;
    SyncEngine a, b;
    assert(a.Initialize(a_store) == SyncStatus::kOk);
    assert(b.Initialize(b_store) == SyncStatus::kOk);
    const uint8_t value = 2;
    assert(a.PutDurableState(1, 1, &value, 1) == SyncStatus::kOk);
    const auto ac = a.Cursors();
    SyncSession as(a), bs(b);
    assert(as.Start(b.Cursors(), UINT32_MAX - 1000) == SyncStatus::kOk);
    assert(bs.Start(ac, 0) == SyncStatus::kOk);
    uint32_t seq = 1;
    Sender sender;
    for (uint32_t elapsed = 0; elapsed <= 9000; elapsed += 3000) {
        as.Poll(sender, seq, UINT32_MAX - 1000 + elapsed);
    }
    assert(sender.sends == 3 && as.Status() == SyncSessionStatus::kTimeout);
    assert(a.PendingDurableCount() == 1);
    assert(as.Start(b.Cursors(), 10000) == SyncStatus::kOk);
    as.Poll(sender, seq, 10000);
    b_store.fail_next_save = true;
    Receive(bs, sender.frame);
    assert(b.InspectIncomingState(1, 1) == SyncStatus::kApplyRequired);
    Sender reply;
    bs.Poll(reply, seq, 10000);
    assert(bs.Status() == SyncSessionStatus::kStoreError);
    Receive(as, reply.frame);
    assert(as.Status() == SyncSessionStatus::kStoreError && a.PendingDurableCount() == 1);
}

void TestCursorWireAndFullStore() {
    SyncCursors cursors{};
    cursors.count = 1;
    cursors.entries[0] = {0x1234, 0x01020304, 0x05060708, 0x090a0b0c};
    const uint8_t expected[] = {1, 1, 0, 0, 0x34, 0x12, 4, 3, 2, 1, 8, 7, 6, 5, 12, 11, 10, 9};
    uint8_t encoded[kSyncCursorValueSize];
    std::size_t size = 0;
    assert(EncodeSyncCursors(cursors, encoded, sizeof(encoded), &size) == ProtocolStatus::kOk);
    assert(size == sizeof(expected) && std::memcmp(encoded, expected, size) == 0);
    SyncCursors decoded{};
    assert(DecodeSyncCursors(encoded, size, &decoded) == ProtocolStatus::kOk);
    encoded[2] = 1;
    assert(DecodeSyncCursors(encoded, size, &decoded) == ProtocolStatus::kMalformedTlv);
    assert(decoded.count == 0);
    encoded[2] = 0;
    assert(DecodeSyncCursors(encoded, size - 1, &decoded) == ProtocolStatus::kMalformedTlv);
    cursors.count = 2;
    cursors.entries[1] = cursors.entries[0];
    assert(EncodeSyncCursors(cursors, encoded, sizeof(encoded), &size) == ProtocolStatus::kMalformedTlv);

    MemoryStore store;
    SyncEngine engine;
    assert(engine.Initialize(store) == SyncStatus::kOk);
    std::array<uint8_t, kDurableValueCapacity> value{};
    for (uint16_t key = 1; key <= kDurableKeyCapacity; ++key) {
        assert(engine.PutDurableState(key, 1, value.data(), value.size()) == SyncStatus::kOk);
        assert(engine.AcceptIncomingState({key, 2, value.data(), value.size()}) == SyncStatus::kOk);
    }
    for (uint32_t id = 1; id <= kCommandDedupeCapacity; ++id) {
        assert(engine.RecordCommandResult(id, 0, value.data(), kCommandResultCapacity) == SyncStatus::kOk);
    }
    assert(store.bytes.size() <= kMaximumSyncRecordSize);
    SyncEngine restarted;
    assert(restarted.Initialize(store) == SyncStatus::kOk);
    assert(restarted.Cursors().count == kDurableKeyCapacity);
    assert(restarted.PendingDurableCount() == kDurableKeyCapacity);
    DurableStateView state{};
    assert(restarted.ReadIncomingState(8, &state) == SyncStatus::kOk && state.value_size == value.size());
}

void TestResourceWindowAndSequenceConflict() {
    MemoryStore a_store, b_store;
    SyncEngine a, b;
    assert(a.Initialize(a_store) == SyncStatus::kOk);
    assert(b.Initialize(b_store) == SyncStatus::kOk);
    const uint8_t value = 1;
    assert(a.PutDurableState(1, 1, &value, 1) == SyncStatus::kOk);
    const auto ac = a.Cursors();
    SyncSession as(a), bs(b);
    assert(as.Start(b.Cursors(), 0) == SyncStatus::kOk);
    assert(bs.Start(ac, 0) == SyncStatus::kOk);
    Sender sender;
    uint32_t sequence = 1;
    as.Poll(sender, sequence, 30000, false);
    assert(sender.sends == 0 && as.Status() == SyncSessionStatus::kActive);
    assert(as.NextWakeMs(30000, false) == UINT32_MAX);
    as.Poll(sender, sequence, 30001);
    assert(sender.sends == 1);
    Receive(bs, sender.frame);
    FrameView decoded{};
    assert(DecodeFrame(sender.frame.data(), sender.frame.size(), 1, 0, &decoded) == ProtocolStatus::kOk);
    std::vector<uint8_t> payload(decoded.payload, decoded.payload + decoded.payload_size);
    payload.back() ^= 1;
    std::vector<uint8_t> conflicting(sender.frame.size());
    std::size_t size = 0;
    assert(EncodeFrame(decoded.header, payload.data(), payload.size(), conflicting.data(), conflicting.size(), &size) == ProtocolStatus::kOk);
    Receive(bs, conflicting);
    assert(bs.Status() == SyncSessionStatus::kProtocolError);
    SyncEngine restarted;
    assert(restarted.Initialize(b_store) == SyncStatus::kOk);
    DurableStateView state{};
    assert(restarted.ReadIncomingState(1, &state) == SyncStatus::kOk && state.value[0] == value);
}

void TestDurableRestartAndAck() {
    MemoryStore store;
    SyncEngine first;
    assert(first.Initialize(store) == SyncStatus::kOk);
    const uint8_t value[] = {1, 2, 3};
    assert(first.PutDurableState(10, 1, value, sizeof(value)) ==
           SyncStatus::kOk);
    assert(first.PutDurableState(10, 1, value, sizeof(value)) ==
           SyncStatus::kDuplicate);
    const uint8_t conflict[] = {1, 2, 4};
    assert(first.PutDurableState(10, 1, conflict, sizeof(conflict)) ==
           SyncStatus::kRevisionConflict);

    SyncEngine restarted;
    assert(restarted.Initialize(store) == SyncStatus::kOk);
    DurableStateView state{};
    assert(restarted.NextDurableState(&state) == SyncStatus::kOk);
    assert(state.key == 10 && state.revision == 1 && state.value_size == 3);
    assert(std::memcmp(state.value, value, sizeof(value)) == 0);
    assert(restarted.AcknowledgeDurableState(10, 1) == SyncStatus::kOk);
    assert(restarted.NextDurableState(&state) == SyncStatus::kNotFound);
    assert(restarted.AcknowledgeDurableState(10, 1) ==
           SyncStatus::kDuplicate);
    assert(restarted.PutDurableState(10, 1, value, sizeof(value)) ==
           SyncStatus::kStaleRevision);

    SyncEngine after_ack;
    assert(after_ack.Initialize(store) == SyncStatus::kOk);
    assert(after_ack.PutDurableState(10, 1, value, sizeof(value)) ==
           SyncStatus::kStaleRevision);
    assert(after_ack.PutDurableState(10, 2, value, sizeof(value)) ==
           SyncStatus::kOk);
}

void TestInboundAndCommandDedupe() {
    MemoryStore store;
    SyncEngine engine;
    assert(engine.Initialize(store) == SyncStatus::kOk);
    assert(engine.InspectIncomingState(22, 1) == SyncStatus::kApplyRequired);
    assert(engine.CommitIncomingState(22, 1) == SyncStatus::kOk);
    assert(engine.InspectIncomingState(22, 1) == SyncStatus::kDuplicate);
    assert(engine.InspectIncomingState(22, 0) == SyncStatus::kInvalidArgument);
    assert(engine.CommitIncomingState(22, 3) == SyncStatus::kOk);
    assert(engine.InspectIncomingState(22, 2) == SyncStatus::kDuplicate);

    const uint8_t reply[] = {'o', 'k'};
    assert(engine.RecordCommandResult(100, 7, reply, sizeof(reply)) ==
           SyncStatus::kOk);
    assert(engine.RecordCommandResult(100, 7, reply, sizeof(reply)) ==
           SyncStatus::kDuplicate);
    CommandResultView result{};
    assert(engine.FindCommandResult(100, &result) == SyncStatus::kOk);
    assert(result.result == 7 && result.payload_size == 2);

    SyncEngine restarted;
    assert(restarted.Initialize(store) == SyncStatus::kOk);
    assert(restarted.InspectIncomingState(22, 3) == SyncStatus::kDuplicate);
    assert(restarted.FindCommandResult(100, &result) == SyncStatus::kOk);
    assert(std::memcmp(result.payload, reply, sizeof(reply)) == 0);
}

void TestCapacityEvictionAndRollback() {
    MemoryStore store;
    SyncEngine engine;
    assert(engine.Initialize(store) == SyncStatus::kOk);
    const uint8_t value = 9;
    for (std::size_t index = 0; index < kDurableKeyCapacity; ++index) {
        assert(engine.PutDurableState(static_cast<uint16_t>(index + 1), 1,
                                      &value, 1) == SyncStatus::kOk);
    }
    assert(engine.PutDurableState(100, 1, &value, 1) ==
           SyncStatus::kOutboxFull);

    store.fail_next_save = true;
    assert(engine.PutDurableState(1, 2, &value, 1) ==
           SyncStatus::kStoreError);
    DurableStateView state{};
    assert(engine.NextDurableState(&state) == SyncStatus::kOk);
    assert(state.key == 1 && state.revision == 1);

    for (uint32_t id = 1; id <= kCommandDedupeCapacity + 1; ++id) {
        assert(engine.RecordCommandResult(1000 + id, 0, nullptr, 0) ==
               SyncStatus::kOk);
    }
    CommandResultView result{};
    assert(engine.FindCommandResult(1001, &result) == SyncStatus::kNotFound);
    assert(engine.FindCommandResult(1002, &result) == SyncStatus::kOk);
}

void TestCorruptRecoveryAndStoreErrors() {
    MemoryStore store;
    SyncEngine source;
    assert(source.Initialize(store) == SyncStatus::kOk);
    const uint8_t value = 1;
    assert(source.PutDurableState(1, 1, &value, 1) == SyncStatus::kOk);
    assert(store.bytes.size() > 20);
    store.bytes[17] ^= 0x80;

    SyncEngine recovered;
    assert(recovered.Initialize(store) == SyncStatus::kCorruptStore);
    assert(recovered.RecoveredFromCorruptStore());
    assert(recovered.PendingDurableCount() == 0);
    assert(recovered.PutDurableState(1, 2, &value, 1) == SyncStatus::kOk);

    MemoryStore broken;
    broken.read_error = true;
    SyncEngine unavailable;
    assert(unavailable.Initialize(broken) == SyncStatus::kStoreError);
    assert(!unavailable.IsInitialized());
}

}  // namespace

int main() {
    TestDurableRestartAndAck();
    TestInboundAndCommandDedupe();
    TestCapacityEvictionAndRollback();
    TestCorruptRecoveryAndStoreErrors();
    TestBidirectionalReconnectWithLostAck();
    TestInFlightReplacementAndExactAck();
    TestPersistenceRollbackAndPeerRegression();
    TestBoundedRetriesAndReceiveSaveFailure();
    TestCursorWireAndFullStore();
    TestResourceWindowAndSequenceConflict();
    return 0;
}
