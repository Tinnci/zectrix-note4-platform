#pragma once

#include <cstddef>
#include <cstdint>

namespace zectrix::time {

enum class SyncSource : uint8_t { None, Rtc, HttpsDate, Companion, Manual };
enum class SyncResult : uint8_t { None, Applied, Unchanged, Invalid, Stale, LowerPriority, ExcessiveStep, Failed };

struct TimeSample {
    int64_t unix_ms = 0;
    int64_t received_us = 0;
    int32_t utc_offset_seconds = 0;
    SyncSource source = SyncSource::None;
    bool has_offset = false;
};

struct SyncStatus {
    SyncSource source = SyncSource::None;
    SyncResult result = SyncResult::None;
    int64_t accepted_us = 0;
    int64_t correction_ms = 0;
    uint32_t rejected = 0;
};

inline constexpr int64_t kTimeSampleLifetimeUs = 30 * 1000000LL;
inline constexpr int64_t kTimeAuthorityHoldUs = 10 * 60 * 1000000LL;

// Parse only IMF-fixdate in GMT. No locale, process TZ or allocation is used.
bool ParseHttpDate(const char* text, std::size_t size, int64_t* unix_ms);
const char* SyncSourceName(SyncSource source);
const char* SyncResultName(SyncResult result);

}  // namespace zectrix::time
