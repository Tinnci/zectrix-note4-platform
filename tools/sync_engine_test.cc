#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "zectrix_sync_engine.h"

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
};

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
    return 0;
}
