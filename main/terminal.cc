#include "terminal_internal.h"
#include "terminal_status.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "zectrix_storage_service.h"
#include "zectrix_language_setting.h"
#include "zectrix_input_service.h"
#include "zectrix_foreground_dispatch.h"

#include "zectrix_boot_guard.h"
#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
#include "zectrix_connectivity_service.h"
#endif

namespace zectrix::terminal {

sdk::Status ToSdkStatus(esp_err_t result) {
    switch (result) {
        case ESP_OK: return sdk::Status::Ok;
        case ESP_ERR_INVALID_ARG: return sdk::Status::InvalidArgument;
        case ESP_ERR_INVALID_STATE: return sdk::Status::InvalidState;
        case ESP_ERR_NOT_FOUND: return sdk::Status::NotFound;
        case ESP_ERR_NO_MEM: return sdk::Status::NoMemory;
        case ESP_ERR_TIMEOUT: return sdk::Status::Timeout;
        case ESP_ERR_NOT_SUPPORTED: return sdk::Status::Unsupported;
        default: return sdk::Status::IoError;
    }
}


TerminalApp::TerminalApp() : ui_(nullptr) { test_states_.fill(ZectrixTestState::kWait); }

void TerminalApp::Run() {
    esp_err_t err = platform_.Initialize();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "platform initialization failed: %s",
                 esp_err_to_name(err));
        return;
    }
    display_ = &platform_.Display();
    input_ = &platform_.Input();
    power_ = &platform_.Power();
    time_ = &platform_.Time();
    storage_ = &platform_.Storage();
    system_ = &platform_.System();
#if CONFIG_ZECTRIX_ENABLE_USB_HOST
    usb_host_ = platform_.Services().Get<host::Channel>();
#endif
    const auto language_result = i18n::RestoreLanguage(*storage_);
    language_saved_ = language_result == ESP_OK || language_result == ESP_ERR_NOT_FOUND ||
        language_result == ESP_ERR_NOT_SUPPORTED;
    if (!language_saved_) ESP_LOGW(kTag, "language preference unavailable; using compiled default");
#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
    connectivity_ = platform_.Services().Get<zectrix::connectivity::ConnectivityService>();
    if (connectivity_) connectivity_->UpdatePower(power_->ReadSnapshot());
#endif
    uint32_t sleep_style = static_cast<uint32_t>(zectrix::app::kSleepCoverDefault);
    const esp_err_t sleep_setting = storage_->GetUInt32(zectrix::app::kSleepCoverSettingKey, &sleep_style);
    if (sleep_setting != ESP_OK) sleep_style = static_cast<uint32_t>(zectrix::app::kSleepCoverDefault);
    sleep_cover_style_ = zectrix::app::SleepCoverSetting(sleep_style);
    sleep_cover_saved_ = sleep_setting == ESP_ERR_NOT_FOUND ||
        (sleep_setting == ESP_OK && sleep_style == static_cast<uint32_t>(sleep_cover_style_));
    if (!sleep_cover_saved_) ESP_LOGW(kTag, "sleep cover preference unavailable; using dashboard");
    tests_ = &platform_.Diagnostics();
    LogHeap("M2-equivalent platform");
    ui_.SetDisplay(display_);
    ui_.SetTime(time_);
    UpdateSystemStatus();
    const auto splash = ui_.ShowSplash();
    if (splash != ESP_OK) ESP_LOGW(kTag, "splash unavailable: %s; continuing to Home", esp_err_to_name(splash));
    if (Wait(1500, false) == ControlResult::kShutdown) PowerOff();

    RunApplicationShell();
}

void TerminalApp::UpdateSystemStatus() {
    platform_.Poll();
    const int64_t now = time_->MonotonicMicroseconds();
    if (now >= next_power_sample_us_) {
        power_snapshot_ = power_->ReadSnapshot();
        display_->ObserveBattery(power_snapshot_.battery_valid && !power_snapshot_.battery_absent ?
            power_snapshot_.battery_mv : 0, time_->MonotonicMicroseconds());
#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
        if (connectivity_) connectivity_->UpdatePower(power_snapshot_);
#endif
        CopyPowerStatus(status_, power_snapshot_);
        next_power_sample_us_ = now + 5000000;
    }
    if (now >= next_clock_sample_us_) {
        const auto clock = time_->Now();
        const auto& value = clock.value;
        status_.time_valid = clock.source != zectrix::time::ClockSource::Uptime;
        status_.hour = status_.time_valid ? static_cast<uint8_t>(value.hour) : 0;
        status_.minute = status_.time_valid ? static_cast<uint8_t>(value.minute) : 0;
        next_clock_sample_us_ = now + 1000000;
    }
#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
    const auto link = connectivity_ ? connectivity_->Snapshot() : zectrix::connectivity::ConnectivitySnapshot{};
    CopyRadioStatus(status_, link);
#endif
    ui_.UpdateStatus(status_);
}

void TerminalApp::RunApplicationShell() {
    if (!ComposeApplications()) {
        ESP_LOGE(kTag, "application catalog is full");
        return;
    }
    sdk::ApplicationRuntime runtime(applications_.data(), applications_.size(), "launcher", *this);
#if CONFIG_ZECTRIX_ENABLE_USB_CLI
    runtime_ = &runtime;
    platform_.SetMaintenanceDelegate(this);
    struct Unbind {
        TerminalApp& owner;
        ~Unbind() { owner.platform_.SetMaintenanceDelegate(nullptr); owner.runtime_ = nullptr; }
    } unbind{*this};
#endif
    auto& health = platform_.Health();
    bool boot_ready = false;
    const auto complete = [&](sdk::Status result) {
        // Entry fallback can succeed while retaining the original SDK error.
        if (!sdk::IsOk(runtime.last_error())) health.SuppressAutomaticApps();
        if (sdk::IsOk(result) && runtime.state() == sdk::LifecycleState::Active)
            result = ToSdkStatus(ui_.RefreshPending());
        const bool recover = health.CompleteForeground(static_cast<int32_t>(result), runtime.foreground_generation());
        if (!sdk::IsOk(result)) ESP_LOGE(kTag, "application step failed: %s", sdk::StatusName(result));
        if (recover && runtime.state() == sdk::LifecycleState::Active) {
            health.BeginRecovery(static_cast<int32_t>(result));
            launcher_back_requested_ = false;
            runtime.Recover(result);
            LogHeap("foreground recovery");
            return true;
        }
        if (!boot_ready && sdk::IsOk(result) && runtime.state() == sdk::LifecycleState::Active &&
            std::strcmp(runtime.foreground_id().c_str(), "launcher") == 0) {
            // A recovery page never confirms a trial; a completed Home frame does.
            const auto confirmed = platform_.ConfirmBoot();
            if (confirmed != update::Result::kOk) {
                ESP_LOGE(kTag, "boot confirmation failed: %s", update::ResultName(confirmed));
                return false;
            }
            boot_ready = true;
            ESP_LOGI(kTag, "launcher ready: applications=%u", static_cast<unsigned>(applications_.size()));
        }
        return true;
    };
    auto started = runtime.Start();
    if (sdk::IsOk(started)) {
        // Retain the boot-evidence marker consumed by the maintenance smoke test.
        LogHeap("M3 runtime active");
        started = runtime.Step();
    }
    if (!complete(started)) return;
    while (runtime.state() == sdk::LifecycleState::Active || runtime.state() == sdk::LifecycleState::Failsafe) {
        sdk::InputEvent event;
        // Pagination, app loading and USB requests yield between bounded slices.
        bool busy = false;
#if CONFIG_ZECTRIX_ENABLE_READER
        busy = reader_busy_;
#endif
#if CONFIG_ZECTRIX_ENABLE_USB_HOST
        busy = busy || (usb_host_ && usb_host_->Session() != 0);
#endif
#if CONFIG_ZECTRIX_ENABLE_RUNTIME
        busy = busy || micro_app_busy_;
#endif
        const TickType_t timeout = busy ? TickType_t{1} : pdMS_TO_TICKS(250);
        const bool received = input_->Wait(&event, timeout);
        UpdateSystemStatus();
        if (received && app::MapNavigation(event) == app::Navigation::Shutdown) {
            runtime.Stop();
            break;
        }
#if CONFIG_ZECTRIX_ENABLE_USB_CLI
        if (maintenance_operation_ >= cli::ControlOperation::kReboot &&
            time_->MonotonicMicroseconds() >= maintenance_ready_us_) {
            // This point is outside every SDK callback, including diagnostic waits.
            executing_maintenance_ = true;
            runtime.Stop();
            break;
        }
#endif
        if (runtime.state() == sdk::LifecycleState::Failsafe) {
            const auto key = received ? app::MapNavigation(event) : app::Navigation::None;
            if (key == app::Navigation::Confirm || key == app::Navigation::Back) {
                const auto reason = runtime.last_error();
                health.BeginRecovery(static_cast<int32_t>(reason));
                auto result = runtime.Recover(reason);
                if (sdk::IsOk(result)) result = runtime.Step();
                if (!complete(result)) return;
            } else {
                // The shared canvas retains failed recovery-page work for retry.
                ui_.RefreshPending();
                health.Progress();
            }
            continue;
        }
        const sdk::Status result = received ? app::DispatchInputBurst(runtime, event,
            [this](sdk::InputEvent* pending) { return input_->Wait(pending, 0); }) : runtime.Idle();
        if (!complete(result)) return;
    }
}

sdk::Status TerminalApp::Shutdown() {
#if CONFIG_ZECTRIX_ENABLE_USB_CLI
    if (executing_maintenance_ && maintenance_operation_ != cli::ControlOperation::kSleep) {
        if (maintenance_operation_ == cli::ControlOperation::kStorageWipe ||
            maintenance_operation_ == cli::ControlOperation::kFactoryReset) {
            const auto result = platform_.ResetUserData(maintenance_operation_ == cli::ControlOperation::kFactoryReset);
            if (result != ESP_OK) ESP_LOGE(kTag, "user data reset incomplete: %s", esp_err_to_name(result));
            else ESP_LOGI(kTag, "user data reset complete");
        }
        platform_.Reboot();
    }
#endif
    PowerOff();
}

#if CONFIG_ZECTRIX_ENABLE_USB_CLI
cli::ControlStatus TerminalApp::ScheduleMaintenance(cli::ControlOperation operation) {
    if (!runtime_ || (runtime_->state() != sdk::LifecycleState::Active && runtime_->state() != sdk::LifecycleState::Failsafe) ||
        maintenance_operation_ >= cli::ControlOperation::kReboot) return cli::ControlStatus::kBusy;
    if (operation < cli::ControlOperation::kReboot || operation > cli::ControlOperation::kFactoryReset)
        return cli::ControlStatus::kInvalidArgument;
    maintenance_operation_ = operation;
    // Give the nonblocking USB writer a short opportunity to report acceptance.
    // A transport failure still means unknown outcome, never permission to retry.
    maintenance_ready_us_ = time_->MonotonicMicroseconds() + 1000000;
    return cli::ControlStatus::kOk;
}

cli::ControlStatus TerminalApp::InspectApps(cli::ControlResult* result) {
    if (!runtime_ || !result) return cli::ControlStatus::kUnavailable;
    auto& a = result->apps;
    static_assert(app::ApplicationCatalog::kCapacity == cli::AppInspection{}.entries.size());
    a.count = static_cast<uint8_t>(applications_.size());
    a.generation = runtime_->foreground_generation();
    a.lifecycle = static_cast<uint8_t>(runtime_->state());
    a.error = static_cast<uint8_t>(runtime_->last_error());
    std::snprintf(a.foreground.data(), a.foreground.size(), "%s", runtime_->foreground_id().c_str());
    for (std::size_t i = 0; i < a.count; ++i) {
        std::snprintf(a.entries[i].id.data(), a.entries[i].id.size(), "%s", applications_.data()[i].id);
        std::snprintf(a.entries[i].label.data(), a.entries[i].label.size(), "%s", applications_.data()[i].display_name);
    }
    auto& s = result->scenes;
    s = guest_inspection_;
    s.depth = scene_snapshot_.depth;
    s.transitioning = scene_snapshot_.transitioning;
    for (std::size_t i = 0; i < s.depth; ++i) s.scenes[i] = {scene_snapshot_.states[i], scene_snapshot_.stack[i]};
    const auto views = ui_.InspectViews();
    for (std::size_t i = 0; i < views.size(); ++i) {
        const auto& v = views[i];
        s.views[i] = {static_cast<int16_t>(v.bounds.x), static_cast<int16_t>(v.bounds.y),
            static_cast<int16_t>(v.bounds.width), static_cast<int16_t>(v.bounds.height),
            v.configured, v.enabled, v.dirty, v.quality};
    }
    return cli::ControlStatus::kOk;
}
#endif

sdk::Status TerminalApp::RequestBack(sdk::ApplicationContext& context) {
    const auto submitted = context.RequestCommand(sdk::AppCommand::Back());
    const bool accepted = submitted == sdk::SubmitResult::Accepted || submitted == sdk::SubmitResult::Superseded;
    if (accepted) launcher_back_requested_ = true;
    return accepted ? sdk::Status::Ok : sdk::Status::InvalidState;
}

void TerminalApp::EnterFailsafe(sdk::Status reason) {
    ESP_LOGE(kTag, "application runtime failsafe: %s",
             sdk::StatusName(reason));
    platform_.Health().SuppressAutomaticApps();
    launcher_back_requested_ = false;
#if CONFIG_ZECTRIX_ENABLE_USB_CLI
    scene_snapshot_ = {};
    guest_inspection_ = {};
#endif
    const auto result = ui_.ShowRecovery();
    if (result != ESP_OK) ESP_LOGW(kTag, "recovery page failed: %s; input and USB remain available", esp_err_to_name(result));
}

void TerminalApp::LogHeap(const char* phase) {
    zectrix::system::SystemSnapshot snapshot;
    const esp_err_t read = system_->ReadSnapshot(&snapshot);
    if (read != ESP_OK) {
        ESP_LOGW(kTag, "heap snapshot failed: %s", esp_err_to_name(read));
        return;
    }
    ESP_LOGI(kTag, "heap %s: free=%lu min=%lu largest=%lu", phase,
             static_cast<unsigned long>(
                 snapshot.diagnostics.free_internal_heap_bytes),
             static_cast<unsigned long>(
                 snapshot.diagnostics.minimum_free_internal_heap_bytes),
             static_cast<unsigned long>(
                 snapshot.diagnostics.largest_internal_heap_block_bytes));
}

ControlResult TerminalApp::Wait(uint32_t duration_ms, bool confirm_returns) {
    const TickType_t duration = pdMS_TO_TICKS(duration_ms);
    const TickType_t start = xTaskGetTickCount();
    while (xTaskGetTickCount() - start < duration) {
        UpdateSystemStatus();
        const esp_err_t draw = ui_.RefreshPending();
        if (draw != ESP_OK) ESP_LOGW(kTag, "status refresh failed: %s", esp_err_to_name(draw));
        sdk::InputEvent event;
        const TickType_t elapsed = xTaskGetTickCount() - start;
        const TickType_t remaining = duration > elapsed ? duration - elapsed : 0;
        if (!input_->Wait(&event,
                          std::min(remaining, pdMS_TO_TICKS(100)))) {
            continue;
        }
        const auto key = app::MapNavigation(event);
        if (key == app::Navigation::Shutdown) {
            return ControlResult::kShutdown;
        }
        if (key == app::Navigation::Back) {
            return ControlResult::kBack;
        }
        if (confirm_returns && key == app::Navigation::Confirm) {
            return ControlResult::kBack;
        }
    }
    return ControlResult::kContinue;
}

[[noreturn]] void TerminalApp::PowerOff() {
    platform_.StopMaintenance();
#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
    const auto stopped = connectivity_ ? connectivity_->Stop() : zectrix::connectivity::ConnectivityResult::kOk;
    if (stopped != zectrix::connectivity::ConnectivityResult::kOk) {
        ESP_LOGW(kTag, "connectivity stop incomplete before shutdown");
    }
#endif
    ESP_LOGI(kTag, "presenting sleep cover before shutdown");
    const esp_err_t cover = ui_.ShowSleepCover(ReadSleepCover(), sleep_cover_style_);
    if (cover != ESP_OK) {
        ESP_LOGW(kTag, "sleep cover failed: %s; attempting blank fallback", esp_err_to_name(cover));
        const auto clear = ui_.ClearDisplay();
        if (clear != ESP_OK) ESP_LOGW(kTag, "blank fallback failed: %s", esp_err_to_name(clear));
    }
    ESP_LOGI(kTag, "releasing platform peripherals before shutdown");
    platform_.Shutdown();
}

void RunTerminal() {
    static TerminalApp app;
    app.Run();
}

}  // namespace zectrix::terminal
