#include "zectrix_sync_engine.h"

#include <algorithm>
#include <cstring>

#include "zectrix_companion_protocol.h"

namespace zectrix::companion {
namespace {

constexpr uint32_t kStoreMagic = 0x4e59535aU;  // "ZSYN" on wire.
constexpr uint16_t kStoreVersion = 1;
constexpr uint16_t kStoreHeaderSize = 16;
constexpr std::size_t kStoreCrcSize = 4;

enum class RecordType : uint8_t {
    kOutbox = 1,
    kCursor = 2,
    kCommand = 3,
};

void PutUInt16(uint8_t* output, uint16_t value) {
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8U);
}

void PutUInt32(uint8_t* output, uint32_t value) {
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8U);
    output[2] = static_cast<uint8_t>(value >> 16U);
    output[3] = static_cast<uint8_t>(value >> 24U);
}

uint16_t GetUInt16(const uint8_t* input) {
    return static_cast<uint16_t>(input[0]) |
           static_cast<uint16_t>(input[1]) << 8U;
}

uint32_t GetUInt32(const uint8_t* input) {
    return static_cast<uint32_t>(input[0]) |
           static_cast<uint32_t>(input[1]) << 8U |
           static_cast<uint32_t>(input[2]) << 16U |
           static_cast<uint32_t>(input[3]) << 24U;
}

bool AddRecordHeader(uint8_t* output, std::size_t capacity,
                     std::size_t* offset, RecordType type,
                     std::size_t content_size) {
    if (content_size > UINT16_MAX || *offset > capacity ||
        capacity - *offset < 4 + content_size) {
        return false;
    }
    output[*offset] = static_cast<uint8_t>(type);
    output[*offset + 1] = 0;
    PutUInt16(output + *offset + 2, static_cast<uint16_t>(content_size));
    *offset += 4;
    return true;
}

}  // namespace

SyncStatus SyncEngine::Initialize(SyncStore& store) {
    Reset();
    store_ = &store;
    std::size_t size = 0;
    const StoreReadStatus status =
        store.Load(persistence_buffer_.data(), persistence_buffer_.size(),
                   &size);
    if (status == StoreReadStatus::kNotFound) return SyncStatus::kOk;
    if (status != StoreReadStatus::kOk) {
        store_ = nullptr;
        return SyncStatus::kStoreError;
    }
    const SyncStatus load_status = LoadState(persistence_buffer_.data(), size);
    if (load_status == SyncStatus::kOk) return SyncStatus::kOk;
    Reset();
    store_ = &store;
    recovered_from_corrupt_ = true;
    return SyncStatus::kCorruptStore;
}

SyncStatus SyncEngine::PutDurableState(uint16_t key, uint32_t revision,
                                       const uint8_t* value,
                                       std::size_t value_size) {
    if (!IsInitialized()) return SyncStatus::kNotInitialized;
    if (key == 0 || revision == 0 || (value == nullptr && value_size != 0)) {
        return SyncStatus::kInvalidArgument;
    }
    if (value_size > kDurableValueCapacity) return SyncStatus::kValueTooLarge;
    const CursorEntry* cursor = FindCursor(key);
    if (cursor != nullptr && revision <= cursor->outbound_acknowledged) {
        return SyncStatus::kStaleRevision;
    }

    DurableEntry* slot = nullptr;
    for (auto& entry : outbox_) {
        if (entry.valid && entry.key == key) {
            slot = &entry;
            break;
        }
    }
    if (slot != nullptr) {
        if (revision < slot->revision) return SyncStatus::kStaleRevision;
        if (revision == slot->revision) {
            if (value_size == slot->value_size &&
                (value_size == 0 ||
                 std::memcmp(value, slot->value.data(), value_size) == 0)) {
                return SyncStatus::kDuplicate;
            }
            return SyncStatus::kRevisionConflict;
        }
    } else {
        for (auto& entry : outbox_) {
            if (!entry.valid) {
                slot = &entry;
                break;
            }
        }
        if (slot == nullptr) return SyncStatus::kOutboxFull;
    }

    const DurableEntry previous = *slot;
    slot->key = key;
    slot->revision = revision;
    slot->value_size = static_cast<uint16_t>(value_size);
    slot->value.fill(0);
    if (value_size != 0) std::memcpy(slot->value.data(), value, value_size);
    slot->valid = true;
    const SyncStatus status = Persist();
    if (status != SyncStatus::kOk) *slot = previous;
    return status;
}

SyncStatus SyncEngine::NextDurableState(DurableStateView* state) const {
    if (!IsInitialized()) return SyncStatus::kNotInitialized;
    if (state == nullptr) return SyncStatus::kInvalidArgument;
    *state = {};
    for (const auto& entry : outbox_) {
        if (!entry.valid) continue;
        state->key = entry.key;
        state->revision = entry.revision;
        state->value = entry.value.data();
        state->value_size = entry.value_size;
        return SyncStatus::kOk;
    }
    return SyncStatus::kNotFound;
}

SyncStatus SyncEngine::AcknowledgeDurableState(uint16_t key,
                                               uint32_t revision) {
    if (!IsInitialized()) return SyncStatus::kNotInitialized;
    DurableEntry* entry = nullptr;
    for (auto& candidate : outbox_) {
        if (candidate.valid && candidate.key == key) {
            entry = &candidate;
            break;
        }
    }
    if (entry == nullptr) {
        const CursorEntry* cursor = FindCursor(key);
        return cursor != nullptr && revision <= cursor->outbound_acknowledged
                   ? SyncStatus::kDuplicate
                   : SyncStatus::kNotFound;
    }
    if (revision < entry->revision) return SyncStatus::kStaleRevision;
    if (revision > entry->revision) return SyncStatus::kRevisionConflict;

    CursorEntry* cursor = FindOrCreateCursor(key);
    if (cursor == nullptr) return SyncStatus::kOutboxFull;
    const DurableEntry prior_entry = *entry;
    const CursorEntry prior_cursor = *cursor;
    entry->valid = false;
    cursor->outbound_acknowledged = revision;
    const SyncStatus status = Persist();
    if (status != SyncStatus::kOk) {
        *entry = prior_entry;
        *cursor = prior_cursor;
    }
    return status;
}

SyncStatus SyncEngine::InspectIncomingState(uint16_t key,
                                            uint32_t revision) const {
    if (!IsInitialized()) return SyncStatus::kNotInitialized;
    if (key == 0 || revision == 0) return SyncStatus::kInvalidArgument;
    const CursorEntry* cursor = FindCursor(key);
    if (cursor != nullptr && revision <= cursor->inbound_applied) {
        return SyncStatus::kDuplicate;
    }
    return SyncStatus::kApplyRequired;
}

SyncStatus SyncEngine::CommitIncomingState(uint16_t key, uint32_t revision) {
    const SyncStatus inspection = InspectIncomingState(key, revision);
    if (inspection != SyncStatus::kApplyRequired) return inspection;
    CursorEntry* cursor = FindOrCreateCursor(key);
    if (cursor == nullptr) return SyncStatus::kOutboxFull;
    const CursorEntry previous = *cursor;
    cursor->inbound_applied = revision;
    const SyncStatus status = Persist();
    if (status != SyncStatus::kOk) *cursor = previous;
    return status;
}

SyncStatus SyncEngine::RecordCommandResult(uint32_t request_id,
                                           uint16_t result,
                                           const uint8_t* payload,
                                           std::size_t payload_size) {
    if (!IsInitialized()) return SyncStatus::kNotInitialized;
    if (request_id == 0 || (payload == nullptr && payload_size != 0)) {
        return SyncStatus::kInvalidArgument;
    }
    if (payload_size > kCommandResultCapacity) {
        return SyncStatus::kValueTooLarge;
    }
    CommandEntry* slot = nullptr;
    for (auto& entry : commands_) {
        if (entry.valid && entry.request_id == request_id) {
            if (entry.result == result && entry.payload_size == payload_size &&
                (payload_size == 0 ||
                 std::memcmp(entry.payload.data(), payload, payload_size) == 0)) {
                return SyncStatus::kDuplicate;
            }
            return SyncStatus::kRevisionConflict;
        }
        if (!entry.valid && slot == nullptr) slot = &entry;
    }
    if (slot == nullptr) {
        slot = &commands_[0];
        for (auto& entry : commands_) {
            if (entry.stamp < slot->stamp) slot = &entry;
        }
    }

    const CommandEntry previous = *slot;
    const uint32_t previous_stamp = next_command_stamp_;
    slot->request_id = request_id;
    slot->stamp = next_command_stamp_++;
    if (next_command_stamp_ == 0) next_command_stamp_ = 1;
    slot->result = result;
    slot->payload_size = static_cast<uint8_t>(payload_size);
    slot->payload.fill(0);
    if (payload_size != 0) std::memcpy(slot->payload.data(), payload, payload_size);
    slot->valid = true;
    const SyncStatus status = Persist();
    if (status != SyncStatus::kOk) {
        *slot = previous;
        next_command_stamp_ = previous_stamp;
    }
    return status;
}

SyncStatus SyncEngine::FindCommandResult(uint32_t request_id,
                                         CommandResultView* result) const {
    if (!IsInitialized()) return SyncStatus::kNotInitialized;
    if (request_id == 0 || result == nullptr) {
        return SyncStatus::kInvalidArgument;
    }
    *result = {};
    for (const auto& entry : commands_) {
        if (!entry.valid || entry.request_id != request_id) continue;
        result->request_id = request_id;
        result->result = entry.result;
        result->payload = entry.payload.data();
        result->payload_size = entry.payload_size;
        return SyncStatus::kOk;
    }
    return SyncStatus::kNotFound;
}

std::size_t SyncEngine::PendingDurableCount() const {
    std::size_t count = 0;
    for (const auto& entry : outbox_) {
        if (entry.valid) ++count;
    }
    return count;
}

void SyncEngine::Reset() {
    store_ = nullptr;
    outbox_ = {};
    cursors_ = {};
    commands_ = {};
    persistence_buffer_.fill(0);
    generation_ = 0;
    next_command_stamp_ = 1;
    recovered_from_corrupt_ = false;
}

SyncStatus SyncEngine::LoadState(const uint8_t* record, std::size_t size) {
    if (record == nullptr || size < kStoreHeaderSize + kStoreCrcSize ||
        size > persistence_buffer_.size()) {
        return SyncStatus::kCorruptStore;
    }
    if (GetUInt32(record) != kStoreMagic ||
        GetUInt16(record + 4) != kStoreVersion ||
        GetUInt16(record + 6) != kStoreHeaderSize ||
        GetUInt16(record + 14) != 0) {
        return SyncStatus::kCorruptStore;
    }
    const std::size_t body_size = GetUInt16(record + 12);
    if (kStoreHeaderSize + body_size + kStoreCrcSize != size ||
        Crc32(record, size - kStoreCrcSize) !=
            GetUInt32(record + size - kStoreCrcSize)) {
        return SyncStatus::kCorruptStore;
    }
    generation_ = GetUInt32(record + 8);
    std::size_t offset = kStoreHeaderSize;
    const std::size_t body_end = kStoreHeaderSize + body_size;
    uint32_t maximum_stamp = 0;
    while (offset < body_end) {
        if (body_end - offset < 4 || record[offset + 1] != 0) {
            return SyncStatus::kCorruptStore;
        }
        const auto type = static_cast<RecordType>(record[offset]);
        const std::size_t content_size = GetUInt16(record + offset + 2);
        offset += 4;
        if (content_size > body_end - offset) return SyncStatus::kCorruptStore;
        const uint8_t* content = record + offset;

        if (type == RecordType::kOutbox) {
            if (content_size < 8) return SyncStatus::kCorruptStore;
            const std::size_t value_size = GetUInt16(content + 6);
            if (value_size > kDurableValueCapacity || 8 + value_size != content_size) {
                return SyncStatus::kCorruptStore;
            }
            DurableEntry* slot = nullptr;
            for (auto& entry : outbox_) {
                if (entry.valid && entry.key == GetUInt16(content)) {
                    return SyncStatus::kCorruptStore;
                }
                if (!entry.valid && slot == nullptr) slot = &entry;
            }
            if (slot == nullptr || GetUInt16(content) == 0 ||
                GetUInt32(content + 2) == 0) {
                return SyncStatus::kCorruptStore;
            }
            slot->key = GetUInt16(content);
            slot->revision = GetUInt32(content + 2);
            slot->value_size = static_cast<uint16_t>(value_size);
            std::memcpy(slot->value.data(), content + 8, value_size);
            slot->valid = true;
        } else if (type == RecordType::kCursor) {
            if (content_size != 10) return SyncStatus::kCorruptStore;
            CursorEntry* slot = nullptr;
            for (auto& entry : cursors_) {
                if (entry.valid && entry.key == GetUInt16(content)) {
                    return SyncStatus::kCorruptStore;
                }
                if (!entry.valid && slot == nullptr) slot = &entry;
            }
            if (slot == nullptr || GetUInt16(content) == 0) {
                return SyncStatus::kCorruptStore;
            }
            slot->key = GetUInt16(content);
            slot->outbound_acknowledged = GetUInt32(content + 2);
            slot->inbound_applied = GetUInt32(content + 6);
            slot->valid = true;
        } else if (type == RecordType::kCommand) {
            if (content_size < 12) return SyncStatus::kCorruptStore;
            const std::size_t payload_size = content[10];
            if (content[11] != 0 || payload_size > kCommandResultCapacity ||
                12 + payload_size != content_size) {
                return SyncStatus::kCorruptStore;
            }
            CommandEntry* slot = nullptr;
            for (auto& entry : commands_) {
                if (entry.valid && entry.request_id == GetUInt32(content)) {
                    return SyncStatus::kCorruptStore;
                }
                if (!entry.valid && slot == nullptr) slot = &entry;
            }
            if (slot == nullptr || GetUInt32(content) == 0 ||
                GetUInt32(content + 4) == 0) {
                return SyncStatus::kCorruptStore;
            }
            slot->request_id = GetUInt32(content);
            slot->stamp = GetUInt32(content + 4);
            slot->result = GetUInt16(content + 8);
            slot->payload_size = static_cast<uint8_t>(payload_size);
            std::memcpy(slot->payload.data(), content + 12, payload_size);
            slot->valid = true;
            maximum_stamp = std::max(maximum_stamp, slot->stamp);
        } else {
            return SyncStatus::kCorruptStore;
        }
        offset += content_size;
    }
    next_command_stamp_ = maximum_stamp + 1;
    if (next_command_stamp_ == 0) next_command_stamp_ = 1;
    return SyncStatus::kOk;
}

SyncStatus SyncEngine::Persist() {
    if (!IsInitialized()) return SyncStatus::kNotInitialized;
    auto& output = persistence_buffer_;
    output.fill(0);
    PutUInt32(output.data(), kStoreMagic);
    PutUInt16(output.data() + 4, kStoreVersion);
    PutUInt16(output.data() + 6, kStoreHeaderSize);
    PutUInt32(output.data() + 8, generation_ + 1);
    std::size_t offset = kStoreHeaderSize;

    for (const auto& entry : outbox_) {
        if (!entry.valid) continue;
        const std::size_t content_size = 8 + entry.value_size;
        if (!AddRecordHeader(output.data(), output.size(), &offset,
                             RecordType::kOutbox, content_size)) {
            return SyncStatus::kStoreError;
        }
        PutUInt16(output.data() + offset, entry.key);
        PutUInt32(output.data() + offset + 2, entry.revision);
        PutUInt16(output.data() + offset + 6, entry.value_size);
        std::memcpy(output.data() + offset + 8, entry.value.data(),
                    entry.value_size);
        offset += content_size;
    }
    for (const auto& entry : cursors_) {
        if (!entry.valid) continue;
        if (!AddRecordHeader(output.data(), output.size(), &offset,
                             RecordType::kCursor, 10)) {
            return SyncStatus::kStoreError;
        }
        PutUInt16(output.data() + offset, entry.key);
        PutUInt32(output.data() + offset + 2, entry.outbound_acknowledged);
        PutUInt32(output.data() + offset + 6, entry.inbound_applied);
        offset += 10;
    }
    for (const auto& entry : commands_) {
        if (!entry.valid) continue;
        const std::size_t content_size = 12 + entry.payload_size;
        if (!AddRecordHeader(output.data(), output.size(), &offset,
                             RecordType::kCommand, content_size)) {
            return SyncStatus::kStoreError;
        }
        PutUInt32(output.data() + offset, entry.request_id);
        PutUInt32(output.data() + offset + 4, entry.stamp);
        PutUInt16(output.data() + offset + 8, entry.result);
        output[offset + 10] = entry.payload_size;
        output[offset + 11] = 0;
        std::memcpy(output.data() + offset + 12, entry.payload.data(),
                    entry.payload_size);
        offset += content_size;
    }

    const std::size_t body_size = offset - kStoreHeaderSize;
    if (body_size > UINT16_MAX || output.size() - offset < kStoreCrcSize) {
        return SyncStatus::kStoreError;
    }
    PutUInt16(output.data() + 12, static_cast<uint16_t>(body_size));
    PutUInt16(output.data() + 14, 0);
    PutUInt32(output.data() + offset, Crc32(output.data(), offset));
    offset += kStoreCrcSize;
    if (!store_->Save(output.data(), offset)) return SyncStatus::kStoreError;
    ++generation_;
    return SyncStatus::kOk;
}

SyncEngine::CursorEntry* SyncEngine::FindOrCreateCursor(uint16_t key) {
    CursorEntry* free_entry = nullptr;
    for (auto& entry : cursors_) {
        if (entry.valid && entry.key == key) return &entry;
        if (!entry.valid && free_entry == nullptr) free_entry = &entry;
    }
    if (free_entry != nullptr) {
        free_entry->key = key;
        free_entry->valid = true;
    }
    return free_entry;
}

const SyncEngine::CursorEntry* SyncEngine::FindCursor(uint16_t key) const {
    for (const auto& entry : cursors_) {
        if (entry.valid && entry.key == key) return &entry;
    }
    return nullptr;
}

}  // namespace zectrix::companion
