#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace zectrix::companion {

constexpr std::size_t kDurableKeyCapacity = 8;
constexpr std::size_t kDurableValueCapacity = 256;
constexpr std::size_t kCommandDedupeCapacity = 16;
constexpr std::size_t kCommandResultCapacity = 32;
constexpr std::size_t kMaximumSyncRecordSize = 3072;

enum class StoreReadStatus : uint8_t {
    kOk = 0,
    kNotFound,
    kError,
};

class SyncStore {
public:
    virtual ~SyncStore() = default;
    virtual StoreReadStatus Load(uint8_t* output, std::size_t capacity,
                                 std::size_t* output_size) = 0;
    virtual bool Save(const uint8_t* input, std::size_t size) = 0;
};

enum class SyncStatus : uint8_t {
    kOk = 0,
    kNotInitialized,
    kInvalidArgument,
    kValueTooLarge,
    kOutboxFull,
    kStaleRevision,
    kRevisionConflict,
    kNotFound,
    kStoreError,
    kCorruptStore,
    kApplyRequired,
    kDuplicate,
};

struct DurableStateView {
    uint16_t key = 0;
    uint32_t revision = 0;
    const uint8_t* value = nullptr;
    std::size_t value_size = 0;
};

struct CommandResultView {
    uint32_t request_id = 0;
    uint16_t result = 0;
    const uint8_t* payload = nullptr;
    std::size_t payload_size = 0;
};

class SyncEngine {
public:
    SyncStatus Initialize(SyncStore& store);
    bool IsInitialized() const { return store_ != nullptr; }
    bool RecoveredFromCorruptStore() const { return recovered_from_corrupt_; }

    SyncStatus PutDurableState(uint16_t key, uint32_t revision,
                               const uint8_t* value,
                               std::size_t value_size);
    SyncStatus NextDurableState(DurableStateView* state) const;
    SyncStatus AcknowledgeDurableState(uint16_t key, uint32_t revision);

    SyncStatus InspectIncomingState(uint16_t key, uint32_t revision) const;
    SyncStatus CommitIncomingState(uint16_t key, uint32_t revision);

    SyncStatus RecordCommandResult(uint32_t request_id, uint16_t result,
                                   const uint8_t* payload,
                                   std::size_t payload_size);
    SyncStatus FindCommandResult(uint32_t request_id,
                                 CommandResultView* result) const;

    std::size_t PendingDurableCount() const;

private:
    struct DurableEntry {
        uint16_t key = 0;
        uint32_t revision = 0;
        uint16_t value_size = 0;
        std::array<uint8_t, kDurableValueCapacity> value{};
        bool valid = false;
    };

    struct CursorEntry {
        uint16_t key = 0;
        uint32_t outbound_acknowledged = 0;
        uint32_t inbound_applied = 0;
        bool valid = false;
    };

    struct CommandEntry {
        uint32_t request_id = 0;
        uint32_t stamp = 0;
        uint16_t result = 0;
        uint8_t payload_size = 0;
        std::array<uint8_t, kCommandResultCapacity> payload{};
        bool valid = false;
    };

    void Reset();
    SyncStatus LoadState(const uint8_t* record, std::size_t size);
    SyncStatus Persist();
    CursorEntry* FindOrCreateCursor(uint16_t key);
    const CursorEntry* FindCursor(uint16_t key) const;

    SyncStore* store_ = nullptr;
    std::array<DurableEntry, kDurableKeyCapacity> outbox_{};
    std::array<CursorEntry, kDurableKeyCapacity> cursors_{};
    std::array<CommandEntry, kCommandDedupeCapacity> commands_{};
    std::array<uint8_t, kMaximumSyncRecordSize> persistence_buffer_{};
    uint32_t generation_ = 0;
    uint32_t next_command_stamp_ = 1;
    bool recovered_from_corrupt_ = false;
};

}  // namespace zectrix::companion
