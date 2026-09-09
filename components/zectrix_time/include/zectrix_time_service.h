#pragma once

#include <cstdint>

#include "esp_err.h"

class ZectrixBoard;

namespace zectrix::time {

struct DateTime {
    int year = 2000;
    int month = 1;
    int day = 1;
    int weekday = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
};

struct RtcTimerStatus {
    bool interrupt_active = false;
    bool flag_set = false;
};

enum class ClockSource : uint8_t { Rtc, System, Uptime };

struct ClockSnapshot {
    DateTime value{};
    ClockSource source = ClockSource::Uptime;
};

class TimeService {
public:
    static esp_err_t Attach(ZectrixBoard& board, TimeService** out_service);
    ~TimeService();

    TimeService(const TimeService&) = delete;
    TimeService& operator=(const TimeService&) = delete;

    int64_t MonotonicMicroseconds() const;
    // Return system time when set, otherwise elapsed hours/minutes since boot.
    // This fallback never invents a calendar date or changes the system clock.
    ClockSnapshot Now() const;
    bool RtcAvailable() const;
    esp_err_t ReadRtc(DateTime* value);
    esp_err_t WriteRtc(const DateTime& value);
    // RTC fields remain local time. An explicitly configured UTC offset is
    // required before using them as the TLS certificate-validation clock.
    esp_err_t SynchronizeSystemClockFromRtc(int32_t utc_offset_seconds);
    esp_err_t StartRtcCountdown(uint8_t seconds);
    esp_err_t ReadRtcTimerStatus(RtcTimerStatus* status);
    esp_err_t StopRtcCountdown();
    esp_err_t ClearRtcTimerFlag();

private:
    explicit TimeService(ZectrixBoard& board) : board_(&board) {}
    ZectrixBoard* board_;
    int32_t utc_offset_seconds_ = 0;
};

}  // namespace zectrix::time
