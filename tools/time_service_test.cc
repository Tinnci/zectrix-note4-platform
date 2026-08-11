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

    assert(service->StartRtcCountdown(1) == ESP_OK);
    assert(board.countdown_seconds == 1);
    board.rtc_interrupt_active = true;
    board.timer_flag = true;
    const RtcTimerStatus status = service->ReadRtcTimerStatus();
    assert(status.interrupt_active && status.flag_set);
    assert(service->StopRtcCountdown() == ESP_OK);
    assert(service->ClearRtcTimerFlag() == ESP_OK);
    assert(!board.timer_flag);

    board.rtc_available = false;
    assert(!service->RtcAvailable());
    assert(service->ReadRtc(&read) == ESP_ERR_NOT_FOUND);
    assert(service->StartRtcCountdown(1) == ESP_ERR_NOT_FOUND);
    delete service;
}
