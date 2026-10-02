#pragma once
#include <array>
#include "esp_err.h"
namespace zectrix::storage { class StorageService; }
namespace zectrix::connectivity {
struct PageTelemetry { bool valid = false; uint8_t battery = 0; uint16_t millivolts = 0; bool charging = false; uint32_t interval_seconds = 21600; };
struct EdgeSettings {
    std::array<char, 64> host{}, path{};
    uint32_t interval_seconds = 21600, budget_ms = 15000;
    uint16_t quiet_start_minute = 0, quiet_end_minute = 0;
    uint8_t minimum_battery_percent = 20;
    bool enabled = false, show_page = false, bthome_enabled = false;
    uint32_t source_generation = 1;
};
bool ValidateEdgeSettings(const EdgeSettings& settings);
class StoredEdgeSettings {
public:
    explicit StoredEdgeSettings(storage::StorageService* storage) : storage_(storage) {}
    esp_err_t Load(EdgeSettings* settings) const;
    esp_err_t Save(const EdgeSettings& settings);
    esp_err_t LoadToken(std::array<char, 65>* token) const;
    esp_err_t SaveToken(const char* token);
private:
    storage::StorageService* storage_;
};
// UTC deadline, local quiet-hours, and calendar are deliberately separate.
struct WakeDecision { uint64_t delay_us = 0; bool sync_due = false; };
bool EdgePowerAllowed(const EdgeSettings&, bool external_power, bool battery_valid, uint8_t battery);
int64_t NextEdgeSync(const EdgeSettings&, int64_t now, int64_t server_suggestion, bool failed);
WakeDecision PlanWake(const EdgeSettings&, int64_t now, int32_t offset_seconds,
                      int64_t next_sync_at, uint64_t calendar_delay_us, bool sync_allowed);
}  // namespace zectrix::connectivity
