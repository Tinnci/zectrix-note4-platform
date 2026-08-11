#include "zectrix_power_service.h"
#include "esp_sleep.h"
#include "zectrix_board.h"

#include <cassert>
#include <csetjmp>

namespace {
esp_sleep_wakeup_cause_t wake_cause = ESP_SLEEP_WAKEUP_UNDEFINED;
TickType_t delays[2] = {};
std::size_t delay_count = 0;
std::jmp_buf shutdown_jump;
}

esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause() { return wake_cause; }
void vTaskDelay(TickType_t ticks) { delays[delay_count++] = ticks; }
[[noreturn]] void esp_deep_sleep_start() { std::longjmp(shutdown_jump, 1); }

int main() {
    using namespace zectrix::power;
    ZectrixBoard board;
    board.power_snapshot = {
        true, 3900, 72, {true, true, false, true, false}};
    PowerService* service = nullptr;
    assert(PowerService::Attach(board, nullptr) == ESP_ERR_INVALID_ARG);
    assert(PowerService::Attach(board, &service) == ESP_OK);
    assert(service != nullptr);

    const PowerSnapshot snapshot = service->ReadSnapshot();
    assert(snapshot.battery_valid && snapshot.battery_mv == 3900);
    assert(snapshot.battery_percent == 72);
    assert(snapshot.external_power_present && snapshot.charging);
    assert(!snapshot.charge_full && snapshot.charge_fault);
    assert(!snapshot.battery_absent);

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

    if (setjmp(shutdown_jump) == 0) service->Shutdown();
    assert(board.power_event_count == 3);
    assert(board.power_events[0] == 2);
    assert(board.power_events[1] == 4);
    assert(board.power_events[2] == 5);
    assert(delay_count == 2 && delays[0] == 100 && delays[1] == 100);
    delete service;
}
