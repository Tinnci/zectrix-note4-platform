#include "zectrix_time_sync.h"
#include "zectrix_calendar.h"

#include <cstring>

namespace zectrix::time {

bool ParseHttpDate(const char* text, std::size_t size, int64_t* unix_ms) {
    if (!text || !unix_ms || size != 29 || text[3] != ',' || text[4] != ' ' ||
        text[7] != ' ' || text[11] != ' ' || text[16] != ' ' || text[19] != ':' ||
        text[22] != ':' || std::memcmp(text + 25, " GMT", 4) != 0) return false;
    const auto number = [text](unsigned offset, unsigned count) {
        int value = 0;
        for (unsigned i = 0; i < count; ++i) {
            const char c = text[offset + i];
            if (c < '0' || c > '9') return -1;
            value = value * 10 + c - '0';
        }
        return value;
    };
    constexpr const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    constexpr const char* weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    DateTime value;
    value.year = number(12, 4); value.day = number(5, 2);
    value.hour = number(17, 2); value.minute = number(20, 2); value.second = number(23, 2);
    value.month = 0; value.weekday = -1;
    for (int i = 0; i < 12; ++i) if (std::memcmp(text + 8, months[i], 3) == 0) value.month = i + 1;
    for (int i = 0; i < 7; ++i) if (std::memcmp(text, weekdays[i], 3) == 0) value.weekday = i;
    if (!IsValid(value)) return false;
    const auto seconds = CalendarSeconds(value);
    if ((seconds / 86400 + 4) % 7 != value.weekday) return false;
    *unix_ms = seconds * 1000;
    return true;
}

const char* SyncSourceName(SyncSource source) {
    switch (source) {
        case SyncSource::Rtc: return "rtc";
        case SyncSource::HttpsDate: return "https-date";
        case SyncSource::Companion: return "companion";
        case SyncSource::Manual: return "manual";
        default: return "none";
    }
}

const char* SyncResultName(SyncResult result) {
    switch (result) {
        case SyncResult::Applied: return "applied";
        case SyncResult::Unchanged: return "unchanged";
        case SyncResult::Invalid: return "invalid";
        case SyncResult::Stale: return "stale";
        case SyncResult::LowerPriority: return "lower-priority";
        case SyncResult::ExcessiveStep: return "excessive-step";
        case SyncResult::Failed: return "failed";
        default: return "none";
    }
}

}  // namespace zectrix::time
