#pragma once

#include "esp_err.h"
#include "zectrix_system_snapshot.h"

namespace zectrix::system {

class HealthWatchdog {
public:
    virtual ~HealthWatchdog() = default;
    virtual uint64_t Milliseconds() const = 0;
    virtual esp_err_t Arm(uint32_t timeout_ms) = 0;
    virtual void Feed() = 0;
    virtual void Disarm() = 0;
};

// Owned by Platform and called only by the foreground owner. No task or heap.
// Destruction deliberately retains reset protection, including failed startup.
class HealthSupervisor final {
public:
    explicit HealthSupervisor(HealthWatchdog& watchdog) : watchdog_(watchdog) {}
    HealthSupervisor(const HealthSupervisor&) = delete;
    HealthSupervisor& operator=(const HealthSupervisor&) = delete;
    esp_err_t Start();
    // Call after completed work, never from a timer, a wait loop or USB polling.
    bool Progress();
    // Returns true when the owner should recover this foreground generation.
    bool CompleteForeground(int32_t error, uint32_t generation);
    void BeginRecovery(int32_t error);
    void SetStorageError(esp_err_t error);
    void SetResetReason(ResetReason reason);
    void SuppressAutomaticApps() { snapshot_.automatic_apps_suppressed = true; }
    bool AutomaticAppsAllowed() const { return !snapshot_.automatic_apps_suppressed; }
    HealthSnapshot Snapshot() const { return snapshot_; }
    // Only after ordered peripheral cleanup, immediately before sleep/reboot.
    void DisarmForPowerTransition();

private:
    HealthWatchdog& watchdog_;
    HealthSnapshot snapshot_{};
    uint32_t generation_ = 0;
};

}  // namespace zectrix::system
