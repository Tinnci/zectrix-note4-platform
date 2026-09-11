#include "zectrix_health_supervisor.h"

#include <cassert>
#include <cstdio>

using namespace zectrix::system;

namespace {
class Watchdog final : public HealthWatchdog {
public:
    uint64_t Milliseconds() const override { return now; }
    esp_err_t Arm(uint32_t timeout) override {
        assert(timeout == kForegroundWatchdogMs);
        ++arms;
        armed = result == ESP_OK;
        return result;
    }
    void Feed() override { assert(armed); ++feeds; }
    void Disarm() override { assert(armed); armed = false; ++disarms; }
    uint64_t now = UINT32_MAX - 1000ULL;
    uint32_t arms = 0, feeds = 0, disarms = 0;
    bool armed = false;
    esp_err_t result = ESP_OK;
};

void TestDeadlines() {
    Watchdog driver;
    {
        HealthSupervisor health(driver);
        // A trial image's watchdog is owned by BootGuard until confirmation.
        assert(health.Progress() && driver.feeds == 0);
        health.DisarmForPowerTransition();
        assert(driver.disarms == 0);
        driver.result = ESP_FAIL;
        assert(health.Start() == ESP_FAIL && !health.Snapshot().watchdog_armed);
        driver.result = ESP_OK;
        assert(health.Start() == ESP_OK && health.Start() == ESP_OK && driver.arms == 2);
        driver.now += 60000;
        assert(health.Progress());
        driver.now += kForegroundWatchdogMs - 1;
        assert(health.Progress());
        const auto feeds = driver.feeds;
        driver.now += kForegroundWatchdogMs;
        assert(!health.Progress() && health.Snapshot().watchdog_expired);
        assert(health.Snapshot().maximum_gap_ms == kForegroundWatchdogMs);
        assert(health.Start() == ESP_ERR_TIMEOUT);
        assert(!health.Progress() && driver.feeds == feeds);
    }
    assert(driver.armed && driver.disarms == 0);
    HealthSupervisor other(driver);
    assert(other.Start() == ESP_OK);
    --driver.now;
    assert(!other.Progress());
    other.DisarmForPowerTransition();
    other.DisarmForPowerTransition();
    assert(!driver.armed && driver.disarms == 1);
}

void TestForegroundAndDegradation() {
    Watchdog driver;
    HealthSupervisor health(driver);
    assert(health.Start() == ESP_OK);
    assert(!health.CompleteForeground(5, 1));
    assert(!health.CompleteForeground(5, 1));
    assert(!health.CompleteForeground(0, 1));
    assert(!health.CompleteForeground(5, 1));
    assert(!health.CompleteForeground(5, 2));
    assert(!health.CompleteForeground(5, 2));
    assert(health.CompleteForeground(5, 2));
    health.BeginRecovery(5);
    assert(!health.AutomaticAppsAllowed());
    assert(health.Snapshot().recoveries == 1 && health.Snapshot().consecutive_failures == 0);
    assert(!health.CompleteForeground(0, 3));
    assert(health.Snapshot().last_error == 5 && health.Snapshot().failures == 6);
    for (auto reason : {ResetReason::Panic, ResetReason::Watchdog, ResetReason::PowerOn}) {
        HealthSupervisor boot(driver);
        boot.SetResetReason(reason);
        assert(boot.Snapshot().recovery_boot == (reason != ResetReason::PowerOn));
        assert(boot.AutomaticAppsAllowed() == (reason == ResetReason::PowerOn));
        boot.SetStorageError(ESP_ERR_NVS_NO_FREE_PAGES);
        assert(!boot.AutomaticAppsAllowed() && boot.Snapshot().storage_error == ESP_ERR_NVS_NO_FREE_PAGES);
    }
    health.DisarmForPowerTransition();
    assert(!health.Snapshot().watchdog_armed);
}

void TestSimulatedWeek() {
    Watchdog driver;
    HealthSupervisor health(driver);
    assert(health.Start() == ESP_OK);
    constexpr uint32_t seconds = 7 * 24 * 60 * 60;
    for (uint32_t second = 0; second < seconds; ++second) {
        driver.now += 1000;
        assert(!health.CompleteForeground(0, second / 30));
    }
    const auto snapshot = health.Snapshot();
    assert(snapshot.heartbeats == seconds && driver.feeds == seconds);
    assert(snapshot.last_progress_ms == driver.now && snapshot.maximum_gap_ms == 1000);
    assert(!snapshot.watchdog_expired && snapshot.failures == 0);
    health.DisarmForPowerTransition();
    std::printf("PASS: health deadlines and %u simulated one-second foreground completions (7 days).\n", seconds);
}
}

int main() {
    TestDeadlines();
    TestForegroundAndDegradation();
    TestSimulatedWeek();
}
