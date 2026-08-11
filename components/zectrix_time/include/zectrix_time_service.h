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

class TimeService {
public:
    static esp_err_t Attach(ZectrixBoard& board, TimeService** out_service);
    ~TimeService();

    TimeService(const TimeService&) = delete;
    TimeService& operator=(const TimeService&) = delete;

    int64_t MonotonicMicroseconds() const;
    bool RtcAvailable() const;
    esp_err_t ReadRtc(DateTime* value);
    esp_err_t WriteRtc(const DateTime& value);
    esp_err_t StartRtcCountdown(uint8_t seconds);
    esp_err_t ReadRtcTimerStatus(RtcTimerStatus* status);
    esp_err_t StopRtcCountdown();
    esp_err_t ClearRtcTimerFlag();

private:
    explicit TimeService(ZectrixBoard& board) : board_(&board) {}
    ZectrixBoard* board_;
};

}  // namespace zectrix::time
