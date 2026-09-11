#include "zectrix_health_supervisor.h"

#include <algorithm>

namespace zectrix::system {
namespace {
void Increment(uint32_t& value) { if (value != UINT32_MAX) ++value; }
}

esp_err_t HealthSupervisor::Start() {
    if (snapshot_.watchdog_expired) return ESP_ERR_TIMEOUT;
    if (snapshot_.watchdog_armed) return ESP_OK;
    const uint64_t now = watchdog_.Milliseconds();
    const auto result = watchdog_.Arm(kForegroundWatchdogMs);
    if (result != ESP_OK) return result;
    snapshot_.last_progress_ms = now;
    snapshot_.watchdog_armed = true;
    return ESP_OK;
}

bool HealthSupervisor::Progress() {
    if (snapshot_.watchdog_expired) return false;
    if (!snapshot_.watchdog_armed) return true;
    const uint64_t now = watchdog_.Milliseconds();
    const uint64_t gap = now - snapshot_.last_progress_ms;
    snapshot_.maximum_gap_ms = std::max(snapshot_.maximum_gap_ms, gap);
    if (now < snapshot_.last_progress_ms || gap >= kForegroundWatchdogMs) {
        // A late callback must not rescue a missed hardware deadline.
        snapshot_.watchdog_expired = true;
        return false;
    }
    watchdog_.Feed();
    snapshot_.last_progress_ms = now;
    Increment(snapshot_.heartbeats);
    return true;
}

bool HealthSupervisor::CompleteForeground(int32_t error, uint32_t generation) {
    Progress();
    if (generation != generation_ || error == 0) snapshot_.consecutive_failures = 0;
    generation_ = generation;
    if (error == 0) return false;
    snapshot_.last_error = error;
    Increment(snapshot_.failures);
    if (snapshot_.consecutive_failures < kForegroundFailureLimit) ++snapshot_.consecutive_failures;
    return snapshot_.consecutive_failures == kForegroundFailureLimit;
}

void HealthSupervisor::BeginRecovery(int32_t error) {
    Increment(snapshot_.recoveries);
    snapshot_.last_error = error;
    snapshot_.consecutive_failures = 0;
    snapshot_.automatic_apps_suppressed = true;
}

void HealthSupervisor::SetStorageError(esp_err_t error) {
    snapshot_.storage_error = error;
    if (error != ESP_OK) snapshot_.automatic_apps_suppressed = true;
}

void HealthSupervisor::SetResetReason(ResetReason reason) {
    snapshot_.recovery_boot = reason == ResetReason::Panic || reason == ResetReason::Watchdog;
    if (snapshot_.recovery_boot) snapshot_.automatic_apps_suppressed = true;
}

void HealthSupervisor::DisarmForPowerTransition() {
    if (!snapshot_.watchdog_armed) return;
    watchdog_.Disarm();
    snapshot_.watchdog_armed = false;
}

}  // namespace zectrix::system
