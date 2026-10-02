#include "zectrix_power_service.h"
#include "esp_sleep.h"
#include "zectrix_board.h"

#include <cassert>
#include <csetjmp>
#include <initializer_list>

namespace {
esp_sleep_wakeup_cause_t wake_cause = ESP_SLEEP_WAKEUP_UNDEFINED;
TickType_t delays[2] = {};
std::size_t delay_count = 0;
std::jmp_buf shutdown_jump;
bool power_ready = false;
uint64_t timer_us = 0;
esp_err_t timer_result = ESP_OK;
}

esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause() { return wake_cause; }
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t value) { timer_us = value; return timer_result; }
int64_t esp_timer_get_time() { return 1234567; }
const char* esp_err_to_name(esp_err_t error) { return error == ESP_OK ? "ESP_OK" : "ESP_FAIL"; }
void vTaskDelay(TickType_t ticks) { delays[delay_count++] = ticks; }
[[noreturn]] void esp_deep_sleep_start() { assert(power_ready); std::longjmp(shutdown_jump, 1); }

int main() {
    using namespace zectrix::power;
    ZectrixBoard board;
    board.power_snapshot = {
        true, 3900, 72, {true, true, false, true, false}};
    PowerService* service = nullptr;
    assert(PowerService::Attach(board, nullptr) == ESP_ERR_INVALID_ARG);
    assert(PowerService::Attach(board, &service) == ESP_OK);
    assert(service != nullptr);
    PowerSnapshot cached;
    int64_t sampled = 0;
    assert(!service->CachedSnapshot(&cached, &sampled));

    const PowerSnapshot snapshot = service->ReadSnapshot();
    assert(snapshot.battery_valid && snapshot.battery_mv == 3900);
    assert(snapshot.battery_percent == 72);
    assert(snapshot.external_power_present && snapshot.charging);
    assert(!snapshot.charge_full && snapshot.charge_fault);
    assert(!snapshot.battery_absent);
    board.power_snapshot.battery_mv = 3200;
    assert(service->CachedSnapshot(&cached, &sampled));
    assert(cached.battery_mv == 3900 && sampled == 1234567);

    struct WakeCase {
        esp_sleep_wakeup_cause_t raw;
        WakeReason expected;
    };
    const WakeCase wake_cases[] = {
        {ESP_SLEEP_WAKEUP_UNDEFINED, WakeReason::PowerOn},
        {ESP_SLEEP_WAKEUP_EXT0, WakeReason::ExternalPin},
        {ESP_SLEEP_WAKEUP_EXT1, WakeReason::ExternalPin},
        {ESP_SLEEP_WAKEUP_TIMER, WakeReason::Timer},
        {ESP_SLEEP_WAKEUP_TOUCHPAD, WakeReason::Touch},
        {ESP_SLEEP_WAKEUP_ULP, WakeReason::ULP},
        {ESP_SLEEP_WAKEUP_GPIO, WakeReason::Other},
    };
    for (const WakeCase& wake_case : wake_cases) {
        wake_cause = wake_case.raw;
        assert(service->GetWakeReason() == wake_case.expected);
    }

    for (const auto cleanup_result : {ESP_OK, ESP_FAIL}) {
        board.power_event_count = delay_count = 0;
        board.peripheral_shutdown_result = cleanup_result;
        board.power_wake_result = cleanup_result;
        power_ready = false;
        if (setjmp(shutdown_jump) == 0) service->Shutdown([](void* context) {
            const auto& board = *static_cast<ZectrixBoard*>(context);
            assert(!power_ready && board.power_event_count == 4 && board.power_events[3] == 7);
            power_ready = true;
        }, &board);
        assert(board.power_event_count == 5);
        assert(board.power_events[0] == 6);
        assert(board.power_events[1] == 2);
        assert(board.power_events[2] == 4);
        assert(board.power_events[3] == 7);
        assert(board.power_events[4] == 5);
        assert(delay_count == 2 && delays[0] == 100 && delays[1] == 100);
    }
    for (const auto result : {ESP_OK, ESP_FAIL}) {
        board.power_wake_result = ESP_OK;
        timer_result = result;
        board.power_event_count = delay_count = 0;
        power_ready = false;
        if (setjmp(shutdown_jump) == 0) service->Shutdown([](void*) { power_ready = true; }, nullptr, 86400000000ULL);
        assert(timer_us == 86400000000ULL);
        assert(board.power_event_count == (result == ESP_OK ? 4 : 5));
        assert(delay_count == (result == ESP_OK ? 1U : 2U));
        wake_cause = ESP_SLEEP_WAKEUP_TIMER;
        assert(service->IsScheduledWake() == (result == ESP_OK));
        wake_cause = ESP_SLEEP_WAKEUP_EXT1;
        assert(!service->IsScheduledWake());
    }
    board.power_wake_result = ESP_FAIL;
    board.power_event_count = delay_count = 0;
    timer_us = 0;
    power_ready = false;
    if (setjmp(shutdown_jump) == 0) service->Shutdown([](void*) { power_ready = true; }, nullptr, 86400000000ULL);
    assert(timer_us == 0 && board.power_event_count == 5);
    wake_cause = ESP_SLEEP_WAKEUP_TIMER;
    assert(!service->IsScheduledWake());
    delete service;
}
