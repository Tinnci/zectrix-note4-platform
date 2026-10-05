#include "terminal_internal.h"
#include "terminal_status.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "note4_foreground_dispatch.h"
#include "note4_input_service.h"
#include "note4_language_setting.h"
#include "note4_log_event.h"
#include "note4_storage_service.h"

#include "note4_boot_guard.h"
#if CONFIG_NOTE4_ENABLE_CONNECTIVITY
#include "note4_connectivity_service.h"
#endif

namespace note4::terminal {

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


TerminalApp::TerminalApp() : ui_(nullptr) { test_states_.fill(Note4TestState::kWait); }

void TerminalApp::Run() {
    esp_err_t err = platform_.Initialize();
    if (err != ESP_OK) {
        NOTE4_LOGE(kTag, "platform_start_failed", "error=%s",
                   note4::log::Token(esp_err_to_name(err)).c_str());
        return;
    }
    display_ = &platform_.Display();
    input_ = &platform_.Input();
    power_ = &platform_.Power();
    time_ = &platform_.Time();
    storage_ = &platform_.Storage();
    uint32_t orientation = static_cast<uint32_t>(display::kDefaultOrientation);
    if (storage_->GetUInt32(display::kOrientationSettingKey, &orientation) != ESP_OK || orientation > 3)
        orientation = static_cast<uint32_t>(display::kDefaultOrientation);
    const auto restored = display_->SetOrientation(static_cast<display::DisplayOrientation>(orientation));
    if (restored != ESP_OK)
        NOTE4_LOGW(kTag, "orientation_restore_failed", "error=%s",
                   note4::log::Token(esp_err_to_name(restored)).c_str());
    system_ = &platform_.System();
#if CONFIG_NOTE4_ENABLE_USB_HOST
    usb_host_ = platform_.Services().Get<host::Channel>();
#endif
    const auto language_result = i18n::RestoreLanguage(*storage_);
    language_saved_ = language_result == ESP_OK || language_result == ESP_ERR_NOT_FOUND ||
        language_result == ESP_ERR_NOT_SUPPORTED;
    if (!language_saved_)
        NOTE4_LOGW(kTag, "language_fallback", "source=builtin");
#if CONFIG_NOTE4_ENABLE_CONNECTIVITY
    connectivity_ = platform_.Services().Get<note4::connectivity::ConnectivityService>();
    if (connectivity_) connectivity_->UpdatePower(power_->ReadSnapshot());
#endif
    uint32_t sleep_style = static_cast<uint32_t>(note4::app::kSleepCoverDefault);
    const esp_err_t sleep_setting = storage_->GetUInt32(note4::app::kSleepCoverSettingKey, &sleep_style);
    if (sleep_setting != ESP_OK) sleep_style = static_cast<uint32_t>(note4::app::kSleepCoverDefault);
    sleep_cover_style_ = note4::app::SleepCoverSetting(sleep_style);
    sleep_cover_saved_ = sleep_setting == ESP_ERR_NOT_FOUND ||
        (sleep_setting == ESP_OK && sleep_style == static_cast<uint32_t>(sleep_cover_style_));
    if (!sleep_cover_saved_)
        NOTE4_LOGW(kTag, "sleep_cover_fallback", "style=dashboard");
    tests_ = &platform_.Diagnostics();
    LogHeap("M2-equivalent platform");
    ui_.SetDisplay(display_);
    ui_.SetTime(time_);
    uint32_t sleep_portrait = app::kSleepPortraitDefault ? 1 : 0;
    const auto portrait_setting = storage_->GetUInt32(app::kSleepPortraitSettingKey, &sleep_portrait);
    sleep_portrait_ = portrait_setting == ESP_OK && sleep_portrait <= 1 ? sleep_portrait == 1 : app::kSleepPortraitDefault;
    ui_.SetSleepPortrait(sleep_portrait_);
    uint32_t digit_style = 0;
    if (storage_->GetUInt32(ui::kDigitStyleSettingKey, &digit_style) != ESP_OK) digit_style = 0;
    ui_.SetDigitStyle(ui::NormalizeDigitStyle(digit_style));
    NOTE4_LOGI(kTag, "sleep_cover_settings", "style=%u portrait=%u",
               static_cast<unsigned>(sleep_cover_style_), static_cast<unsigned>(sleep_portrait_));
    // A trial OTA image must reach the existing Home-frame confirmation path;
    // an unattended lock-screen wake must never confirm or bypass that trial.
    if (power_->IsScheduledWake() && !platform_.Boot().ReadBootStatus().confirmation_pending) {
        NOTE4_LOGI(kTag, "calendar_refresh_wake", "");
#if CONFIG_NOTE4_ENABLE_CONNECTIVITY
        LoadEdgeConfiguration();
        if (!RefreshEdgeOnWake()) {
            if (connectivity_ && edge_configuration_valid_ && edge_settings_.bthome_enabled &&
                !platform_.Health().Snapshot().recovery_boot && platform_.Health().Snapshot().storage_error == ESP_OK) {
                auto telemetry = edge_settings_;
                telemetry.enabled = true;
                const auto clock = time_->Status();
                const auto now = time_->UnixSeconds();
                if (clock.utc_offset_known && connectivity::PlanWake(telemetry, now,
                    clock.utc_offset_seconds, now, 0, true).sync_due)
                    connectivity_->BroadcastPower(power_->ReadSnapshot());
            }
            PowerOff();
        }
        // A physical key cancels the burst and returns to the normal UI.
        if (connectivity_ && connectivity_->Initialize() != connectivity::ConnectivityResult::kOk)
            NOTE4_LOGW(kTag, "connectivity_interactive_start_failed", "");
#else
        PowerOff();
#endif
    }
    UpdateSystemStatus();
    const auto splash = ui_.ShowSplash();
    if (splash != ESP_OK)
        NOTE4_LOGW(kTag, "splash_failed", "error=%s next=home",
                   note4::log::Token(esp_err_to_name(splash)).c_str());
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
#if CONFIG_NOTE4_ENABLE_CONNECTIVITY
        if (connectivity_) connectivity_->UpdatePower(power_snapshot_);
#endif
        CopyPowerStatus(status_, power_snapshot_);
        next_power_sample_us_ = now + 5000000;
    }
    if (now >= next_clock_sample_us_) {
        const auto clock = time_->Now();
        const auto& value = clock.value;
        status_.time_valid = clock.source != note4::time::ClockSource::Uptime;
        status_.hour = status_.time_valid ? static_cast<uint8_t>(value.hour) : 0;
        status_.minute = status_.time_valid ? static_cast<uint8_t>(value.minute) : 0;
        next_clock_sample_us_ = now + 1000000;
    }
#if CONFIG_NOTE4_ENABLE_CONNECTIVITY
    const auto link = connectivity_ ? connectivity_->Snapshot() : note4::connectivity::ConnectivitySnapshot{};
    CopyRadioStatus(status_, link);
#endif
    ui_.UpdateStatus(status_);
}

void TerminalApp::RunApplicationShell() {
    if (!ComposeApplications()) {
        NOTE4_LOGE(kTag, "application_catalog_full", "");
        return;
    }
    sdk::ApplicationRuntime runtime(applications_.data(), applications_.size(), "launcher", *this);
#if CONFIG_NOTE4_ENABLE_USB_CLI
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
        if (!launcher_open_target_.empty()) {
            if (runtime.state() == sdk::LifecycleState::Active &&
                std::strcmp(runtime.foreground_id().c_str(), "launcher") == 0)
                launcher_failed_target_ = launcher_open_target_;
            launcher_open_target_ = {};
        }
        // Entry fallback can succeed while retaining the original SDK error.
        if (!sdk::IsOk(runtime.last_error())) health.SuppressAutomaticApps();
        if (sdk::IsOk(result) && runtime.state() == sdk::LifecycleState::Active)
            result = ToSdkStatus(ui_.RefreshPending());
        const bool recover = health.CompleteForeground(static_cast<int32_t>(result), runtime.foreground_generation());
        if (!sdk::IsOk(result))
            NOTE4_LOGE(kTag, "application_step_failed", "error=%s",
                       note4::log::Token(sdk::StatusName(result)).c_str());
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
                NOTE4_LOGE(kTag, "boot_confirm_failed", "error=%s",
                           note4::log::Token(update::ResultName(confirmed)).c_str());
                return false;
            }
            boot_ready = true;
            NOTE4_LOGI(kTag, "launcher_ready", "count=%u",
                       static_cast<unsigned>(applications_.size()));
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
#if CONFIG_NOTE4_ENABLE_READER
        busy = reader_busy_;
#endif
#if CONFIG_NOTE4_ENABLE_USB_HOST
        busy = busy || (usb_host_ && usb_host_->Session() != 0);
#endif
#if CONFIG_NOTE4_ENABLE_RUNTIME
        busy = busy || micro_app_busy_;
#endif
        const TickType_t timeout = busy ? TickType_t{1} : pdMS_TO_TICKS(250);
        const bool received = input_->Wait(&event, timeout);
        UpdateSystemStatus();
        if (received && app::MapNavigation(event) == app::Navigation::Shutdown) {
            runtime.Stop();
            break;
        }
#if CONFIG_NOTE4_ENABLE_USB_CLI
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
#if CONFIG_NOTE4_ENABLE_USB_CLI
    if (executing_maintenance_ && maintenance_operation_ != cli::ControlOperation::kSleep) {
        if (maintenance_operation_ == cli::ControlOperation::kStorageWipe ||
            maintenance_operation_ == cli::ControlOperation::kFactoryReset) {
            const auto result = platform_.ResetUserData(maintenance_operation_ == cli::ControlOperation::kFactoryReset);
            if (result != ESP_OK)
                NOTE4_LOGE(kTag, "user_data_reset_failed", "error=%s",
                           note4::log::Token(esp_err_to_name(result)).c_str());
            else
                NOTE4_LOGI(kTag, "user_data_reset_done", "");
        }
        platform_.Reboot();
    }
#endif
    PowerOff();
}

#if CONFIG_NOTE4_ENABLE_USB_CLI
cli::ControlStatus TerminalApp::HandleDisplaySettings(const cli::ControlRequest& request, cli::ControlResult* result) {
    if (!result || !storage_ || !display_) return cli::ControlStatus::kUnavailable;
    if (request.operation == cli::ControlOperation::kDisplayConfigure) {
        if (!request.confirmed || request.origin != cli::Origin::kUsbLocal) return cli::ControlStatus::kDenied;
        if (request.values[1] > 1 || request.values[0] > (request.values[1] ? 1u : 3u))
            return cli::ControlStatus::kInvalidArgument;
    } else if (request.operation != cli::ControlOperation::kDisplaySettings) return cli::ControlStatus::kInvalidArgument;
    uint32_t screen = static_cast<uint32_t>(display::kDefaultOrientation);
    uint32_t sleep = app::kSleepPortraitDefault ? 1 : 0;
    const auto screen_read = storage_->GetUInt32(display::kOrientationSettingKey, &screen);
    const auto sleep_read = storage_->GetUInt32(app::kSleepPortraitSettingKey, &sleep);
    if (screen_read == ESP_ERR_NOT_FOUND) screen = static_cast<uint32_t>(display::kDefaultOrientation);
    if (sleep_read == ESP_ERR_NOT_FOUND) sleep = app::kSleepPortraitDefault ? 1 : 0;
    if ((screen_read != ESP_OK && screen_read != ESP_ERR_NOT_FOUND) ||
        (sleep_read != ESP_OK && sleep_read != ESP_ERR_NOT_FOUND) || screen > 3 || sleep > 1)
        return cli::ControlStatus::kUnavailable;
    if (request.operation == cli::ControlOperation::kDisplayConfigure) {
        const char* key = request.values[1] ? app::kSleepPortraitSettingKey : display::kOrientationSettingKey;
        if (storage_->SetUInt32(key, request.values[0]) != ESP_OK) return cli::ControlStatus::kUnavailable;
        if (request.values[1]) sleep = request.values[0];
        else screen = request.values[0];
    }
    result->display_settings = {static_cast<uint8_t>(display_->orientation()), static_cast<uint8_t>(screen), sleep == 1};
    return cli::ControlStatus::kOk;
}

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
    NOTE4_LOGE(kTag, "application_failsafe", "reason=%s",
               note4::log::Token(sdk::StatusName(reason)).c_str());
    platform_.Health().SuppressAutomaticApps();
    launcher_back_requested_ = false;
#if CONFIG_NOTE4_ENABLE_USB_CLI
    scene_snapshot_ = {};
    guest_inspection_ = {};
#endif
    const auto result = ui_.ShowRecovery();
    if (result != ESP_OK)
        NOTE4_LOGW(kTag, "recovery_page_failed", "error=%s input=available usb=available",
                   note4::log::Token(esp_err_to_name(result)).c_str());
}

void TerminalApp::LogHeap(const char* phase) {
    note4::system::SystemSnapshot snapshot;
    const esp_err_t read = system_->ReadSnapshot(&snapshot);
    if (read != ESP_OK) {
        NOTE4_LOGW(kTag, "heap_snapshot_failed", "error=%s",
                   note4::log::Token(esp_err_to_name(read)).c_str());
        return;
    }
    NOTE4_LOGI(kTag, "heap_snapshot", "phase=%s free_bytes=%lu minimum_bytes=%lu largest_bytes=%lu",
               note4::log::Token(phase).c_str(),
               static_cast<unsigned long>(snapshot.diagnostics.free_internal_heap_bytes),
               static_cast<unsigned long>(snapshot.diagnostics.minimum_free_internal_heap_bytes),
               static_cast<unsigned long>(snapshot.diagnostics.largest_internal_heap_block_bytes));
}

ControlResult TerminalApp::Wait(uint32_t duration_ms, bool confirm_returns) {
    const TickType_t duration = pdMS_TO_TICKS(duration_ms);
    const TickType_t start = xTaskGetTickCount();
    while (xTaskGetTickCount() - start < duration) {
        UpdateSystemStatus();
        const esp_err_t draw = ui_.RefreshPending();
        if (draw != ESP_OK)
            NOTE4_LOGW(kTag, "status_refresh_failed", "error=%s",
                       note4::log::Token(esp_err_to_name(draw)).c_str());
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
    // Capture committed sync values before stopping their connectivity owner.
    const auto sleep_snapshot = ReadSleepCover();
#if CONFIG_NOTE4_ENABLE_CONNECTIVITY
    const auto stopped = connectivity_ ? connectivity_->Stop() : note4::connectivity::ConnectivityResult::kOk;
    if (stopped != note4::connectivity::ConnectivityResult::kOk) {
        NOTE4_LOGW(kTag, "connectivity_stop_retry", "phase=shutdown");
    }
#endif
    NOTE4_LOGI(kTag, "sleep_cover_start", "");
    esp_err_t cover = ESP_ERR_NOT_FOUND;
#if CONFIG_NOTE4_ENABLE_CONNECTIVITY
    LoadEdgeConfiguration();
    cover = PresentEdgeCover();
#endif
    if (cover != ESP_OK) cover = PresentSleepCover(sleep_snapshot, sleep_cover_style_);
    if (cover != ESP_OK) {
        NOTE4_LOGW(kTag, "sleep_cover_failed", "error=%s fallback=blank",
                   note4::log::Token(esp_err_to_name(cover)).c_str());
        const auto clear = ui_.ClearDisplay();
        if (clear != ESP_OK)
            NOTE4_LOGW(kTag, "blank_fallback_failed", "error=%s",
                       note4::log::Token(esp_err_to_name(clear)).c_str());
    }
    NOTE4_LOGI(kTag, "shutdown_start", "");
    // Measure from the current clock after display I/O and cleanup preparation,
    // not the earlier cover snapshot, so slow full refreshes do not accumulate.
    const bool battery_low = sleep_snapshot.power.battery_valid &&
        !sleep_snapshot.power.external_power_present && sleep_snapshot.power.battery_percent <= 5;
    auto wake_after_us = battery_low ? 0 : app::SleepRefreshDelayUs(sleep_cover_style_, time_->Now());
#if CONFIG_NOTE4_ENABLE_CONNECTIVITY
    if (!battery_low) wake_after_us = NextWakeDelay();
#endif
    NOTE4_LOGI(kTag, "sleep_cover_presented", "style=%u portrait=%u result=%s wake_after_us=%llu",
               static_cast<unsigned>(sleep_cover_style_), static_cast<unsigned>(sleep_portrait_),
               note4::log::Token(esp_err_to_name(cover)).c_str(),
               static_cast<unsigned long long>(wake_after_us));
    platform_.Shutdown(wake_after_us);
}

void RunTerminal() {
    static TerminalApp app;
    app.Run();
}

}  // namespace note4::terminal
