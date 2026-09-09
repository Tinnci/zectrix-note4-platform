#pragma once

#include <cstdint>

#include "esp_err.h"
#include "zectrix_calendar.h"

class ZectrixBoard;
namespace zectrix::storage { class StorageService; }

namespace zectrix::time {

inline constexpr char kRtcUtcOffsetKey[] = "rtc_utc_offset";

struct RtcTimerStatus {
    bool interrupt_active = false;
    bool flag_set = false;
};

enum class ClockSource : uint8_t { Rtc, System, Uptime };

struct ClockSnapshot {
    DateTime value{};
    ClockSource source = ClockSource::Uptime;
};

struct ClockStatus {
    int32_t utc_offset_seconds = 0;
    bool utc_offset_known = false;
    bool rtc_persisted = false;
    bool persistence_pending = false;
    esp_err_t last_error = ESP_OK;
};

class TimeService {
public:
    static esp_err_t Attach(ZectrixBoard& board, TimeService** out_service);
    ~TimeService();

    TimeService(const TimeService&) = delete;
    TimeService& operator=(const TimeService&) = delete;

    // The platform owner initializes before connectivity/TLS and polls on its
    // foreground loop. An unset or absent RTC is a recoverable startup result.
    esp_err_t Initialize(storage::StorageService& storage);
    void Poll();
    int64_t MonotonicMicroseconds() const;
    // No I2C or storage work occurs during rendering. Legacy local RTC time
    // without a known offset may be displayed, but cannot initialize UTC/TLS.
    ClockSnapshot Now() const;
    ClockStatus Status() const { return status_; }
    // ESP_OK means UTC was set for this boot. Status reports RTC persistence
    // separately; failed saves are retried by Poll() and must not be shown as
    // durable. Only the foreground owner may calibrate, initialize or poll.
    esp_err_t SetLocalTime(const DateTime& value, int32_t utc_offset_seconds);
    esp_err_t SetUnixTime(int64_t unix_milliseconds, int32_t utc_offset_seconds);
    bool RtcAvailable() const;
    esp_err_t ReadRtc(DateTime* value);
    esp_err_t StartRtcCountdown(uint8_t seconds);
    esp_err_t ReadRtcTimerStatus(RtcTimerStatus* status);
    esp_err_t StopRtcCountdown();
    esp_err_t ClearRtcTimerFlag();

private:
    explicit TimeService(ZectrixBoard& board) : board_(&board) {}
    esp_err_t Restore();
    esp_err_t Persist();
    ZectrixBoard* board_;
    storage::StorageService* storage_ = nullptr;
    ClockStatus status_{};
    ClockSource system_source_ = ClockSource::System;
    int64_t local_rtc_seconds_ = 0;
    int64_t local_rtc_sample_us_ = 0;
    int64_t next_retry_us_ = 0;
    int32_t stored_offset_seconds_ = 0;
    bool stored_offset_known_ = false;
};

}  // namespace zectrix::time
