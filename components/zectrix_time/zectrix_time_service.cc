#include "zectrix_time_service.h"

#include <ctime>
#include <new>
#include <sys/time.h>

#include "esp_timer.h"
#include "zectrix_board.h"
#include "zectrix_storage_service.h"

namespace zectrix::time {
namespace {

constexpr int64_t kRetryIntervalUs = 60 * 1000000LL;
constexpr int64_t kFirstCalendarSecond = 946684800;
constexpr int64_t kLastCalendarSecond = 4102444799;

tm ToTm(const DateTime& value) {
    tm result = {};
    result.tm_year = value.year - 1900;
    result.tm_mon = value.month - 1;
    result.tm_mday = value.day;
    result.tm_wday = value.weekday;
    result.tm_hour = value.hour;
    result.tm_min = value.minute;
    result.tm_sec = value.second;
    return result;
}

DateTime FromTm(const tm& value) {
    return DateTime{value.tm_year + 1900, value.tm_mon + 1, value.tm_mday,
                    value.tm_wday, value.tm_hour, value.tm_min, value.tm_sec};
}

bool CalendarFromSeconds(int64_t seconds, DateTime* value) {
    if (seconds < kFirstCalendarSecond || seconds > kLastCalendarSecond) return false;
    const time_t raw = static_cast<time_t>(seconds);
    if (static_cast<int64_t>(raw) != seconds) return false;
    tm fields{};
    if (!gmtime_r(&raw, &fields)) return false;
    *value = FromTm(fields);
    return IsValid(*value);
}

esp_err_t SetSystemClock(int64_t milliseconds) {
    timeval clock{};
    clock.tv_sec = static_cast<time_t>(milliseconds / 1000);
    if (static_cast<int64_t>(clock.tv_sec) != milliseconds / 1000) return ESP_ERR_INVALID_ARG;
    clock.tv_usec = static_cast<suseconds_t>((milliseconds % 1000) * 1000);
    return settimeofday(&clock, nullptr) == 0 ? ESP_OK : ESP_FAIL;
}

}  // namespace

esp_err_t TimeService::Attach(ZectrixBoard& board, TimeService** out_service) {
    if (out_service == nullptr) return ESP_ERR_INVALID_ARG;
    *out_service = new (std::nothrow) TimeService(board);
    return *out_service == nullptr ? ESP_ERR_NO_MEM : ESP_OK;
}

TimeService::~TimeService() = default;

esp_err_t TimeService::Initialize(storage::StorageService& storage) {
    if (storage_ != nullptr) return ESP_ERR_INVALID_STATE;
    storage_ = &storage;
    status_.last_error = Restore();
    next_retry_us_ = MonotonicMicroseconds() + kRetryIntervalUs;
    return status_.last_error;
}

esp_err_t TimeService::Restore() {
    int32_t offset = 0;
    const esp_err_t loaded = storage_->GetInt32(kRtcUtcOffsetKey, &offset);
    stored_offset_known_ = loaded == ESP_OK && IsValidUtcOffset(offset);
    if (stored_offset_known_) {
        stored_offset_seconds_ = offset;
        status_.utc_offset_seconds = offset;
        status_.utc_offset_known = true;
    }

    DateTime value{};
    const esp_err_t read = ReadRtc(&value);
    if (read != ESP_OK) return read;
    local_rtc_seconds_ = CalendarSeconds(value);
    local_rtc_sample_us_ = MonotonicMicroseconds();
    if (!stored_offset_known_) return loaded == ESP_OK ? ESP_ERR_INVALID_RESPONSE : loaded;

    const esp_err_t restored = SetSystemClock((local_rtc_seconds_ - offset) * 1000);
    if (restored == ESP_OK) {
        status_.rtc_persisted = true;
        system_source_ = ClockSource::Rtc;
    }
    return restored;
}

void TimeService::Poll() {
    if (!storage_ || MonotonicMicroseconds() < next_retry_us_) return;
    if (status_.persistence_pending) status_.last_error = Persist();
    else if (!status_.rtc_persisted) status_.last_error = Restore();
    next_retry_us_ = MonotonicMicroseconds() + kRetryIntervalUs;
}

int64_t TimeService::MonotonicMicroseconds() const {
    return esp_timer_get_time();
}

ClockSnapshot TimeService::Now() const {
    const int64_t system_time = std::time(nullptr);
    DateTime value{};
    if (system_time >= kFirstCalendarSecond - kMaximumUtcOffsetSeconds &&
        system_time <= kLastCalendarSecond + kMaximumUtcOffsetSeconds &&
        CalendarFromSeconds(system_time + status_.utc_offset_seconds, &value)) {
        return {value, system_source_};
    }
    if (local_rtc_seconds_ != 0 && CalendarFromSeconds(local_rtc_seconds_ +
            (MonotonicMicroseconds() - local_rtc_sample_us_) / 1000000, &value)) {
        return {value, ClockSource::Rtc};
    }
    const int64_t seconds = MonotonicMicroseconds() / 1000000;
    return {{0, 0, 0, 0, static_cast<int>(seconds / 3600),
             static_cast<int>((seconds / 60) % 60), static_cast<int>(seconds % 60)},
            ClockSource::Uptime};
}

bool TimeService::RtcAvailable() const {
    return board_ != nullptr && board_->HasRtc();
}

esp_err_t TimeService::ReadRtc(DateTime* value) {
    if (value == nullptr) return ESP_ERR_INVALID_ARG;
    if (!RtcAvailable()) return ESP_ERR_NOT_FOUND;
    tm raw = {};
    if (!board_->ReadRtc(&raw)) return ESP_FAIL;
    const DateTime candidate = FromTm(raw);
    if (!IsValid(candidate)) return ESP_ERR_INVALID_RESPONSE;
    *value = candidate;
    return ESP_OK;
}

esp_err_t TimeService::SetLocalTime(const DateTime& value, int32_t utc_offset_seconds) {
    if (!IsValid(value) || !IsValidUtcOffset(utc_offset_seconds)) return ESP_ERR_INVALID_ARG;
    return SetUnixTime((CalendarSeconds(value) - utc_offset_seconds) * 1000, utc_offset_seconds);
}

esp_err_t TimeService::SetUnixTime(int64_t unix_milliseconds, int32_t utc_offset_seconds) {
    if (!IsValidUtcOffset(utc_offset_seconds) ||
        unix_milliseconds < (kFirstCalendarSecond - kMaximumUtcOffsetSeconds) * 1000 ||
        unix_milliseconds > (kLastCalendarSecond + kMaximumUtcOffsetSeconds) * 1000 + 999) {
        return ESP_ERR_INVALID_ARG;
    }
    DateTime value{};
    if (!CalendarFromSeconds(unix_milliseconds / 1000 + utc_offset_seconds, &value)) return ESP_ERR_INVALID_ARG;
    if (!storage_) return ESP_ERR_INVALID_STATE;
    const esp_err_t set = SetSystemClock(unix_milliseconds);
    if (set != ESP_OK) return set;
    system_source_ = ClockSource::System;
    local_rtc_seconds_ = 0;
    status_.utc_offset_seconds = utc_offset_seconds;
    status_.utc_offset_known = true;
    status_.rtc_persisted = false;
    status_.persistence_pending = true;
    status_.last_error = Persist();
    next_retry_us_ = MonotonicMicroseconds() + kRetryIntervalUs;
    return ESP_OK;
}

esp_err_t TimeService::Persist() {
    if (!RtcAvailable()) return ESP_ERR_NOT_FOUND;
    // STOP is also the native invalid state across a reset between NVS and
    // RTC writes. Never resume a partially written calendar or mismatched TZ.
    if (!board_->StopRtcClock()) return ESP_FAIL;
    if (!stored_offset_known_ || stored_offset_seconds_ != status_.utc_offset_seconds) {
        const esp_err_t saved = storage_->SetInt32(kRtcUtcOffsetKey, status_.utc_offset_seconds);
        if (saved != ESP_OK) return saved;
        stored_offset_known_ = true;
        stored_offset_seconds_ = status_.utc_offset_seconds;
    }
    DateTime value{};
    if (!CalendarFromSeconds(static_cast<int64_t>(std::time(nullptr)) + status_.utc_offset_seconds, &value))
        return ESP_ERR_INVALID_STATE;
    if (!board_->WriteRtc(ToTm(value))) return ESP_FAIL;
    status_.rtc_persisted = true;
    status_.persistence_pending = false;
    return ESP_OK;
}

esp_err_t TimeService::StartRtcCountdown(uint8_t seconds) {
    if (!RtcAvailable()) return ESP_ERR_NOT_FOUND;
    return board_->StartRtcCountdown(seconds) ? ESP_OK : ESP_FAIL;
}

esp_err_t TimeService::ReadRtcTimerStatus(RtcTimerStatus* status) {
    if (status == nullptr) return ESP_ERR_INVALID_ARG;
    if (!RtcAvailable()) return ESP_ERR_NOT_FOUND;
    bool flag_set = false;
    const esp_err_t result = board_->ReadRtcTimerFlag(&flag_set);
    if (result != ESP_OK) return result;
    *status = {board_->IsRtcInterruptActive(), flag_set};
    return ESP_OK;
}

esp_err_t TimeService::StopRtcCountdown() {
    if (!RtcAvailable()) return ESP_ERR_NOT_FOUND;
    return board_->StopRtcCountdown() ? ESP_OK : ESP_FAIL;
}

esp_err_t TimeService::ClearRtcTimerFlag() {
    if (!RtcAvailable()) return ESP_ERR_NOT_FOUND;
    return board_->ClearRtcTimerFlag() ? ESP_OK : ESP_FAIL;
}

}  // namespace zectrix::time
