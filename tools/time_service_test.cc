#include "zectrix_time_service.h"
#include "zectrix_board.h"

#include <cassert>

static std::int64_t monotonic_time = 1234567;
std::int64_t esp_timer_get_time() { return monotonic_time; }

int main() {
    using namespace zectrix::time;
    ZectrixBoard board;
    TimeService* service = nullptr;
    assert(TimeService::Attach(board, &service) == ESP_OK);
    assert(service != nullptr);
    assert(service->MonotonicMicroseconds() == monotonic_time);
    assert(service->RtcAvailable());

    const DateTime written{2026, 8, 11, 2, 17, 30, 45};
    assert(service->WriteRtc(written) == ESP_OK);
    assert(board.rtc_value.tm_year == 126);
    assert(board.rtc_value.tm_mon == 7);
    DateTime read;
    assert(service->ReadRtc(&read) == ESP_OK);
    assert(read.year == written.year && read.month == written.month);
    assert(read.day == written.day && read.weekday == written.weekday);
    assert(read.hour == written.hour && read.minute == written.minute);
    assert(read.second == written.second);

    const tm last_valid_write = board.rtc_value;
    const DateTime invalid_values[] = {
        {1999, 12, 31, 5, 23, 59, 59},
        {2100, 1, 1, 5, 0, 0, 0},
        {2026, 13, 1, 1, 0, 0, 0},
        {2026, 2, 29, 0, 0, 0, 0},
        {2024, 2, 30, 5, 0, 0, 0},
        {2026, 8, 11, 7, 0, 0, 0},
        {2026, 8, 11, 2, 24, 0, 0},
        {2026, 8, 11, 2, 0, 60, 0},
        {2026, 8, 11, 2, 0, 0, 60},
    };
    for (const DateTime& invalid : invalid_values) {
        assert(service->WriteRtc(invalid) == ESP_ERR_INVALID_ARG);
        assert(board.rtc_value.tm_year == last_valid_write.tm_year);
        assert(board.rtc_value.tm_mon == last_valid_write.tm_mon);
        assert(board.rtc_value.tm_mday == last_valid_write.tm_mday);
    }
    assert(service->WriteRtc({2024, 2, 29, 4, 12, 0, 0}) == ESP_OK);

    const DateTime unchanged = read;
    board.rtc_value.tm_mon = 12;
    assert(service->ReadRtc(&read) == ESP_ERR_INVALID_RESPONSE);
    assert(read.year == unchanged.year && read.month == unchanged.month);
    board.rtc_value = last_valid_write;

    assert(service->StartRtcCountdown(1) == ESP_OK);
    assert(board.countdown_seconds == 1);
    board.rtc_interrupt_active = true;
    board.timer_flag = true;
    RtcTimerStatus status;
    assert(service->ReadRtcTimerStatus(&status) == ESP_OK);
    assert(status.interrupt_active && status.flag_set);
    board.rtc_io_ok = false;
    assert(service->ReadRtcTimerStatus(&status) == ESP_FAIL);
    board.rtc_io_ok = true;
    assert(service->StopRtcCountdown() == ESP_OK);
    assert(service->ClearRtcTimerFlag() == ESP_OK);
    assert(!board.timer_flag);

    board.rtc_available = false;
    assert(!service->RtcAvailable());
    assert(service->ReadRtc(&read) == ESP_ERR_NOT_FOUND);
    assert(service->StartRtcCountdown(1) == ESP_ERR_NOT_FOUND);
    assert(service->ReadRtcTimerStatus(&status) == ESP_ERR_NOT_FOUND);
    delete service;
}
