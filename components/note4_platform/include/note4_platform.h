#pragma once

#include <cstdint>

#include "esp_err.h"
#include "note4_service_registry.h"

class Note4SelfTest;

namespace note4::display { class DisplayService; }
namespace note4::connectivity { class ConnectivityService; }
namespace note4::input { class InputService; }
namespace note4::power { class PowerService; }
namespace note4::storage { class StorageService; }
namespace note4::system { class SystemService; class HealthSupervisor; }
namespace note4::time { class TimeService; }
namespace note4::update { class BootGuard; class UpdateService; enum class Result : uint8_t; }
namespace note4::cli { class MaintenanceDelegate; }

namespace note4 {

class Platform {
public:
    Platform() = default;
    ~Platform();

    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;

    esp_err_t Initialize();
    bool IsInitialized() const { return initialized_; }
    // Optional lookup is valid even before initialization and after shutdown.
    // Only Platform can register, start or stop providers.
    const ServiceRegistry& Services() const { return services_; }
    // Foreground safe point for copied service events and time persistence.
    void Poll();
    // Application-owner safe points and shutdown admission control.
    void PollMaintenance();
    void StopMaintenance();
    void SetMaintenanceDelegate(cli::MaintenanceDelegate* delegate);
    // The first successful Home frame hands trial protection to runtime health.
    update::Result ConfirmBoot();
    system::HealthSupervisor& Health() const;
    // The shell has exited its foreground before either operation is called.
    esp_err_t ResetUserData(bool factory);
    [[noreturn]] void Reboot();
    // Called by the application owner after its final display update. Releases
    // service-owned peripherals before PowerService cuts rails and sleeps.
    [[noreturn]] void Shutdown(uint64_t wake_after_us = 0);

    // Initialize() must return ESP_OK before an application calls an accessor.
    // A contract violation stops in an assertion instead of dereferencing an
    // unchecked null implementation pointer.
    display::DisplayService& Display() const;
    input::InputService& Input() const;
    power::PowerService& Power() const;
    time::TimeService& Time() const;
    storage::StorageService& Storage() const;
    system::SystemService& System() const;
    update::BootGuard& Boot() const;
    // Optional service accessors also require the corresponding module enabled.
    connectivity::ConnectivityService& Connectivity() const;
    update::UpdateService& Update() const;
    Note4SelfTest& Diagnostics() const;

private:
    void ReleaseServices();
    void ResetServices();
    struct Impl;
    Impl* impl_ = nullptr;
    ServiceRegistry services_;
    bool initialization_attempted_ = false;
    bool initialized_ = false;
};

}  // namespace note4
