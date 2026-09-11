#include "zectrix_time_service.h"
#include "zectrix_board.h"
#include "zectrix_storage_service.h"

#include <cassert>
#include <cstring>
#include <limits>
#include <memory>
#include <sys/time.h>

namespace {
int64_t monotonic_time = 0;
timeval system_clock{};
int clock_writes = 0;
int clock_result = 0;
bool offset_saved = false;
int32_t saved_offset = 0;
esp_err_t storage_read_result = ESP_OK, storage_write_result = ESP_OK;
unsigned storage_writes = 0;
ZectrixBoard* writing_board = nullptr;
}

int64_t esp_timer_get_time() { return monotonic_time; }

extern "C" time_t time(time_t* value)
#if defined(__linux__)
    noexcept
#endif
{
    if (value) *value = system_clock.tv_sec;
    return system_clock.tv_sec;
}

// Intercept the platform call so this test never changes the host clock.
extern "C" int settimeofday(const timeval* value, const struct timezone*)
#if defined(__linux__)
    noexcept
#endif
{
    ++clock_writes;
    if (clock_result == 0) system_clock = *value;
    return clock_result;
}

namespace zectrix::storage {
struct StorageService::Impl {};
esp_err_t StorageService::Create(StorageService** output) {
    *output = new StorageService(nullptr);
    return ESP_OK;
}
StorageService::~StorageService() = default;
esp_err_t StorageService::GetInt32(const char* key, int32_t* value) const {
    assert(std::strcmp(key, time::kRtcUtcOffsetKey) == 0);
    if (storage_read_result != ESP_OK) return storage_read_result;
    if (!offset_saved) return ESP_ERR_NOT_FOUND;
    *value = saved_offset;
    return ESP_OK;
}
esp_err_t StorageService::SetInt32(const char* key, int32_t value) {
    assert(std::strcmp(key, time::kRtcUtcOffsetKey) == 0);
    // A power cut while this write commits must leave the RTC untrustworthy.
    assert(writing_board && writing_board->rtc_stopped);
    ++storage_writes;
    if (storage_write_result != ESP_OK) return storage_write_result;
    saved_offset = value;
    offset_saved = true;
    return ESP_OK;
}
}  // namespace zectrix::storage

namespace {
using namespace zectrix::time;

void Reset() {
    monotonic_time = 1234567;
    system_clock = {};
    clock_writes = clock_result = 0;
    offset_saved = false;
    saved_offset = 0;
    storage_read_result = storage_write_result = ESP_OK;
    storage_writes = 0;
}

std::unique_ptr<TimeService> Attach(ZectrixBoard& board) {
    TimeService* service = nullptr;
    assert(TimeService::Attach(board, &service) == ESP_OK && service);
    writing_board = &board;
    return std::unique_ptr<TimeService>(service);
}

void SetRetainedDate(ZectrixBoard& board, const DateTime& value) {
    board.rtc_value = {};
    board.rtc_value.tm_year = value.year - 1900;
    board.rtc_value.tm_mon = value.month - 1;
    board.rtc_value.tm_mday = value.day;
    board.rtc_value.tm_wday = value.weekday;
    board.rtc_value.tm_hour = value.hour;
    board.rtc_value.tm_min = value.minute;
    board.rtc_value.tm_sec = value.second;
}

void TestRestore(zectrix::storage::StorageService& storage) {
    Reset();
    ZectrixBoard board;
    SetRetainedDate(board, {2024, 2, 29, 4, 12, 0, 0});
    auto service = Attach(board);
    assert(service->MonotonicMicroseconds() == monotonic_time);
    assert(service->Now().source == ClockSource::Uptime);
    // Legacy local calendar remains useful, but an unknown UTC offset cannot
    // become a certificate-validation timestamp.
    assert(service->Initialize(storage) == ESP_ERR_NOT_FOUND);
    assert(service->Now().source == ClockSource::Rtc && service->Now().value.hour == 12);
    assert(system_clock.tv_sec == 0 && clock_writes == 0);
    monotonic_time += 61 * 1000000LL;
    assert(service->Now().value.minute == 1);
    assert(board.rtc_reads == 1 && board.rtc_writes == 0 && board.rtc_stops == 0);

    assert(service->SetLocalTime({2024, 2, 29, 0, 12, 0, 0}, 8 * 3600) == ESP_OK);
    assert(system_clock.tv_sec == 1709179200);
    assert(board.rtc_value.tm_wday == 4);
    assert(service->Status().rtc_persisted && !service->Status().persistence_pending);
    assert(saved_offset == 8 * 3600 && offset_saved);
    const unsigned writes = board.rtc_writes, stops = board.rtc_stops;
    service.reset();
    assert(!board.rtc_stopped && board.rtc_writes == writes && board.rtc_stops == stops);

    // A new owner sees the retained hardware calendar after MCU power loss.
    system_clock = {};
    service = Attach(board);
    assert(service->Initialize(storage) == ESP_OK);
    assert(system_clock.tv_sec == 1709179200);
    assert(service->Now().source == ClockSource::Rtc);
    assert(service->Now().value.year == 2024 && service->Now().value.day == 29);
    assert(board.rtc_writes == writes && board.rtc_stops == stops);
    const unsigned reads = board.rtc_reads;
    for (int i = 0; i < 90; ++i) {
        monotonic_time += 1000000;
        ++system_clock.tv_sec;
        service->Poll();
        assert(service->Now().source == ClockSource::Rtc);
    }
    assert(board.rtc_reads == reads && storage_writes == 1);
}

void TestCalendar(zectrix::storage::StorageService& storage) {
    Reset();
    ZectrixBoard board;
    auto service = Attach(board);
    assert(service->Initialize(storage) == ESP_ERR_INVALID_RESPONSE);
    const DateTime invalid[] = {
        {1999, 12, 31, 5, 23, 59, 59}, {2100, 1, 1, 5, 0, 0, 0},
        {2026, 13, 1, 1, 0, 0, 0}, {2026, 2, 29, 0, 0, 0, 0},
        {2024, 2, 30, 5, 0, 0, 0}, {2026, 8, 11, 7, 0, 0, 0},
        {2026, 8, 11, 2, 24, 0, 0}, {2026, 8, 11, 2, 0, 60, 0},
        {2026, 8, 11, 2, 0, 0, 60}, {2026, 0, 0, 0, 0, 0, 0},
    };
    for (const auto& value : invalid) assert(service->SetLocalTime(value, 0) == ESP_ERR_INVALID_ARG);
    for (const int32_t offset : {-50401, 50401})
        assert(service->SetLocalTime({2024, 1, 1, 0, 0, 0, 0}, offset) == ESP_ERR_INVALID_ARG);
    for (const int64_t sample : {int64_t{-1}, std::numeric_limits<int64_t>::max(),
                                 std::numeric_limits<int64_t>::min()})
        assert(service->SetUnixTime(sample, 0) == ESP_ERR_INVALID_ARG);
    assert(board.rtc_writes == 0 && board.rtc_stops == 0 && clock_writes == 0);

    struct Boundary { DateTime date; int32_t offset; int64_t unix_seconds; };
    const Boundary boundaries[] = {
        {{2000, 1, 1, 6, 0, 0, 0}, 14 * 3600, 946634400},
        {{2000, 3, 1, 3, 0, 0, 0}, -9 * 3600, 951901200},
        {{2024, 1, 1, 1, 0, 0, 0}, 0, 1704067200},
        {{2024, 2, 29, 4, 0, 0, 0}, 5 * 3600 + 45 * 60, 1709144100},
        {{2099, 12, 31, 4, 23, 59, 59}, -14 * 3600, 4102495199},
    };
    for (const auto& test : boundaries) {
        assert(service->SetLocalTime(test.date, test.offset) == ESP_OK);
        assert(system_clock.tv_sec == test.unix_seconds);
        assert(service->Now().value.year == test.date.year && service->Now().value.hour == test.date.hour);
        service.reset();
        system_clock = {};
        service = Attach(board);
        assert(service->Initialize(storage) == ESP_OK);
        assert(system_clock.tv_sec == test.unix_seconds);
    }
    assert(service->SetUnixTime(1709179200123, 8 * 3600) == ESP_OK);
    assert(system_clock.tv_usec == 123000 && service->Now().value.hour == 12);
    const unsigned writes = board.rtc_writes, stops = board.rtc_stops;
    clock_result = -1;
    assert(service->SetLocalTime({2025, 1, 1, 0, 0, 0, 0}, 0) == ESP_FAIL);
    assert(board.rtc_writes == writes && board.rtc_stops == stops);
    assert(service->Now().value.year == 2024);
    clock_result = 0;
}

void TestInterruptedCalibration(zectrix::storage::StorageService& storage) {
    for (unsigned stage = 0; stage < 4; ++stage) {
        Reset();
        ZectrixBoard board;
        auto service = Attach(board);
        service->Initialize(storage);
        assert(service->SetLocalTime({2024, 2, 29, 4, 12, 0, 0}, 0) == ESP_OK);
        if (stage == 0) board.rtc_stop_ok = false;
        if (stage == 1) storage_write_result = ESP_FAIL;
        if (stage == 2) board.rtc_write_ok = false;
        if (stage == 3) board.rtc_resume_ok = false;
        assert(service->SetLocalTime({2026, 1, 1, 4, 8, 0, 0}, 8 * 3600) == ESP_OK);
        assert(service->Status().last_error == ESP_FAIL);
        assert(service->Status().persistence_pending && !service->Status().rtc_persisted);
        assert(service->Now().value.year == 2026);
        assert(board.rtc_stopped == (stage != 0));

        // Simulate reboot at each interrupted stage. Before STOP succeeds the
        // old RTC/offset pair is intact; afterward it must not restore UTC.
        const auto calibrated_system = system_clock;
        system_clock = {};
        auto rebooted = Attach(board);
        const auto restored = rebooted->Initialize(storage);
        assert((restored == ESP_OK) == (stage == 0));
        if (stage != 0) assert(rebooted->Now().source == ClockSource::Uptime && system_clock.tv_sec == 0);
        rebooted.reset();
        system_clock = calibrated_system;

        const unsigned writes = board.rtc_writes;
        board.rtc_stop_ok = board.rtc_write_ok = board.rtc_resume_ok = true;
        storage_write_result = ESP_OK;
        service->Poll();
        assert(board.rtc_writes == writes);
        monotonic_time += 60 * 1000000LL;
        system_clock.tv_sec += 60;
        service->Poll();
        assert(service->Status().rtc_persisted && !service->Status().persistence_pending);
        assert(!board.rtc_stopped && board.rtc_value.tm_min == 1);
        assert(saved_offset == 8 * 3600);
    }
}

void TestRecoveryAndTimers(zectrix::storage::StorageService& storage) {
    Reset();
    ZectrixBoard board;
    SetRetainedDate(board, {2026, 8, 11, 2, 17, 30, 45});
    offset_saved = true;
    saved_offset = 8 * 3600;
    board.rtc_voltage_low = true;
    auto service = Attach(board);
    assert(service->Initialize(storage) == ESP_FAIL);
    assert(service->Now().source == ClockSource::Uptime);
    for (int i = 0; i < 20; ++i) service->Poll();
    assert(board.rtc_reads == 1);
    board.rtc_voltage_low = false;
    monotonic_time += 60 * 1000000LL;
    service->Poll();
    assert(service->Now().source == ClockSource::Rtc && service->Now().value.hour == 17);
    assert(board.rtc_writes == 0);

    DateTime read{2024, 1, 1, 1, 1, 1, 1};
    board.rtc_value.tm_mon = 12;
    assert(service->ReadRtc(&read) == ESP_ERR_INVALID_RESPONSE && read.year == 2024);
    assert(service->ReadRtc(nullptr) == ESP_ERR_INVALID_ARG);
    assert(service->StartRtcCountdown(1) == ESP_OK && board.countdown_seconds == 1);
    board.rtc_interrupt_active = board.timer_flag = true;
    RtcTimerStatus status;
    assert(service->ReadRtcTimerStatus(&status) == ESP_OK && status.interrupt_active && status.flag_set);
    board.rtc_io_ok = false;
    assert(service->ReadRtcTimerStatus(&status) == ESP_FAIL);
    board.rtc_io_ok = true;
    assert(service->StopRtcCountdown() == ESP_OK && service->ClearRtcTimerFlag() == ESP_OK);
    assert(!board.timer_flag);
    board.rtc_available = false;
    assert(service->ReadRtc(&read) == ESP_ERR_NOT_FOUND);
    assert(service->StartRtcCountdown(1) == ESP_ERR_NOT_FOUND);
    assert(service->ReadRtcTimerStatus(&status) == ESP_ERR_NOT_FOUND);
    assert(service->SetUnixTime(1709179200123, 8 * 3600) == ESP_OK);
    assert(service->Status().last_error == ESP_ERR_NOT_FOUND);
    assert(service->Now().source == ClockSource::System && service->Status().persistence_pending);

    Reset();
    service = Attach(board);
    assert(service->Initialize(storage) == ESP_ERR_NOT_FOUND);
    monotonic_time = (25LL * 3600 + 42 * 60 + 9) * 1000000;
    const auto uptime = service->Now();
    assert(uptime.source == ClockSource::Uptime && uptime.value.year == 0);
    assert(uptime.value.hour == 25 && uptime.value.minute == 42 && uptime.value.second == 9);
}

void TestUnknownOffset(zectrix::storage::StorageService& storage) {
    for (unsigned failure = 0; failure < 2; ++failure) {
        Reset();
        ZectrixBoard board;
        SetRetainedDate(board, {2024, 1, 1, 1, 12, 0, 0});
        if (failure == 0) { offset_saved = true; saved_offset = 50401; }
        else storage_read_result = ESP_FAIL;
        auto service = Attach(board);
        assert(service->Initialize(storage) != ESP_OK);
        assert(!service->Status().utc_offset_known);
        assert(service->Now().source == ClockSource::Rtc && clock_writes == 0);
    }
}

void TestSourceArbitration(zectrix::storage::StorageService& storage) {
    Reset();
    ZectrixBoard board;
    auto service = Attach(board);
    service->Initialize(storage);
    TimeSample https{1709179200000, monotonic_time, 0, SyncSource::HttpsDate, false};
    assert(service->ApplySample(https) == SyncResult::ExcessiveStep);
    TimeSample phone{https.unix_ms, monotonic_time, 28800, SyncSource::Companion, true};
    assert(service->ApplySample(phone) == SyncResult::Applied);
    assert(service->Status().rtc_persisted && service->Synchronization().source == SyncSource::Companion);
    assert(service->Now().value.hour == 12);
    const auto writes = board.rtc_writes;
    assert(service->ApplySample(phone) == SyncResult::Unchanged && board.rtc_writes == writes);
    assert(service->ApplySample(https) == SyncResult::LowerPriority);

    assert(service->SetUnixTime(phone.unix_ms, 28800) == ESP_OK);
    assert(service->Synchronization().source == SyncSource::Manual && service->Synchronization().correction_ms == 0);
    phone.unix_ms += 10000;
    assert(service->ApplySample(phone) == SyncResult::LowerPriority);
    monotonic_time += kTimeAuthorityHoldUs;
    https.received_us = monotonic_time - 2500000;
    https.unix_ms = service->UnixSeconds() * 1000 + 10000;
    assert(service->ApplySample(https) == SyncResult::Applied);
    assert(system_clock.tv_sec == 1709179212 && system_clock.tv_usec == 500000);
    assert(service->Status().utc_offset_seconds == 28800);
    assert(service->Synchronization().correction_ms == 12500);
    phone.received_us = monotonic_time;
    phone.unix_ms = system_clock.tv_sec * 1000;
    phone.utc_offset_seconds = -18000;
    assert(service->ApplySample(phone) == SyncResult::Applied && service->Now().value.day == 28);

    for (const auto received : {monotonic_time + 1, int64_t{-1}, monotonic_time - kTimeSampleLifetimeUs - 1}) {
        phone.received_us = received;
        assert(service->ApplySample(phone) == SyncResult::Stale);
    }
    phone.received_us = monotonic_time;
    for (const auto ms : {INT64_MIN, INT64_MAX, int64_t{0}}) {
        phone.unix_ms = ms;
        assert(service->ApplySample(phone) == SyncResult::Invalid);
    }
    monotonic_time += kTimeAuthorityHoldUs;
    https.received_us = monotonic_time;
    https.unix_ms = service->UnixSeconds() * 1000 + 300001;
    assert(service->ApplySample(https) == SyncResult::ExcessiveStep);
    https.unix_ms = service->UnixSeconds() * 1000 - 300001;
    assert(service->ApplySample(https) == SyncResult::ExcessiveStep);
    https.unix_ms = service->UnixSeconds() * 1000;
    https.has_offset = true;
    assert(service->ApplySample(https) == SyncResult::ExcessiveStep);
    https.has_offset = false;
    clock_result = -1;
    https.unix_ms += 10000;
    assert(service->ApplySample(https) == SyncResult::Failed);
    assert(service->Synchronization().source == SyncSource::Companion);

    // UTC without timezone must not be overwritten by a subsequent RTC restore.
    Reset();
    SetRetainedDate(board, {2020, 1, 1, 3, 12, 0, 0});
    service = Attach(board);
    service->Initialize(storage);
    TimeSample utc{1709179200000, monotonic_time, 0, SyncSource::Companion, false};
    assert(service->ApplySample(utc) == SyncResult::Applied);
    assert(!service->Status().utc_offset_known && !service->Status().persistence_pending);
    const auto reads = board.rtc_reads;
    monotonic_time += 120000000;
    service->Poll();
    assert(board.rtc_reads == reads && service->Now().value.year == 2024);
    utc = {946634400000, monotonic_time, 50400, SyncSource::Companion, true};
    assert(service->ApplySample(utc) == SyncResult::Applied && service->Now().value.year == 2000);
    utc = {4102495199000, monotonic_time, -50400, SyncSource::Companion, true};
    assert(service->ApplySample(utc) == SyncResult::Applied && service->Now().value.year == 2099);
}

void TestHttpDate() {
    int64_t ms = 0;
    const char* valid = "Thu, 29 Feb 2024 04:00:00 GMT";
    assert(ParseHttpDate(valid, std::strlen(valid), &ms) && ms == 1709179200000);
    for (const auto* value : {"Thu, 29 Feb 2023 04:00:00 GMT", "Fri, 29 Feb 2024 04:00:00 GMT",
            "Thu, 29 Feb 2024 24:00:00 GMT", "Thu, 29 Feb 2024 04:00:60 GMT",
            "Thu, 29 Feb 2024 04:00:00 UTC", "Thu, 29 Feb 2024 04:00:00 GMT ",
            "Thu, 29 Xxx 2024 04:00:00 GMT", "Mon, 01 Jan 2100 00:00:00 GMT"})
        assert(!ParseHttpDate(value, std::strlen(value), &ms));
    assert(!ParseHttpDate(nullptr, 29, &ms) && !ParseHttpDate(valid, 29, nullptr));
}
}  // namespace

int main() {
    zectrix::storage::StorageService* storage = nullptr;
    assert(zectrix::storage::StorageService::Create(&storage) == ESP_OK);
    TestRestore(*storage);
    TestCalendar(*storage);
    TestInterruptedCalibration(*storage);
    TestRecoveryAndTimers(*storage);
    TestUnknownOffset(*storage);
    TestSourceArbitration(*storage);
    TestHttpDate();
    delete storage;
}
