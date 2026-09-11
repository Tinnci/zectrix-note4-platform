#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace zectrix::input {

struct TraceRecord {
    uint64_t sequence = 0;
    int64_t timestamp_us = 0;
    uint8_t button = 0, action = 0;
    bool queued = false;
};

struct TraceBatch {
    std::array<TraceRecord, 4> records{};
    uint64_t cursor = 0, lost = 0;
    uint8_t count = 0;
};

// The board serializes this mirror with its existing short button lock.
// Overwriting observation records never changes the application event queue.
class InputTrace {
public:
    static constexpr std::size_t kCapacity = 16;
    void Push(int64_t timestamp_us, uint8_t button, uint8_t action, bool queued) {
        const auto sequence = ++sequence_;
        records_[(sequence - 1) % kCapacity] = {sequence, timestamp_us, button, action, queued};
    }
    TraceBatch Read(uint64_t cursor) const {
        TraceBatch batch;
        if (cursor == 0 || cursor > sequence_ + 1) cursor = sequence_ + 1;
        const uint64_t oldest = sequence_ >= kCapacity ? sequence_ - kCapacity + 1 : 1;
        if (cursor < oldest) { batch.lost = oldest - cursor; cursor = oldest; }
        while (cursor <= sequence_ && batch.count < batch.records.size())
            batch.records[batch.count++] = records_[(cursor++ - 1) % kCapacity];
        batch.cursor = cursor;
        return batch;
    }

private:
    std::array<TraceRecord, kCapacity> records_{};
    uint64_t sequence_ = 0;
};

}  // namespace zectrix::input
