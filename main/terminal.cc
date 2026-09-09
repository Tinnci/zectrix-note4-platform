#include "terminal_internal.h"

#include <algorithm>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "zectrix_storage_service.h"
#include "zectrix_input_service.h"

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
    ESP_ERROR_CHECK(ui_.ShowSplash());
    Wait(1500, false);

    RunApplicationShell();
}

void TerminalApp::UpdateSystemStatus() {
    platform_.Poll();
    const int64_t now = time_->MonotonicMicroseconds();
    if (now >= next_power_sample_us_) {
        power_snapshot_ = power_->ReadSnapshot();
#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
        if (connectivity_) connectivity_->UpdatePower(power_snapshot_);
#endif
        status_.battery_valid = power_snapshot_.battery_valid && !power_snapshot_.battery_absent;
        status_.battery_percent = std::min<uint8_t>(power_snapshot_.battery_percent, 100);
        status_.charging = power_snapshot_.charging;
        status_.external_power = power_snapshot_.external_power_present;
        status_.charge_fault = power_snapshot_.charge_fault;
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
    using Indicator = zectrix::ui::RadioIndicator;
    using Ble = zectrix::connectivity::ConnectivityState;
    switch (link.state) {
        case Ble::kStopped: status_.ble = Indicator::Off; break;
        case Ble::kIdle:
        case Ble::kAdvertising: status_.ble = Indicator::Ready; break;
        case Ble::kPairing:
        case Ble::kSecuring: status_.ble = Indicator::Busy; break;
        case Ble::kSecure:
        case Ble::kLinkReady:
        case Ble::kProtocolNegotiatedLocal: status_.ble = Indicator::Connected; break;
        case Ble::kFault: status_.ble = Indicator::Fault; break;
    }
    using Wifi = zectrix::connectivity::WifiBackendState;
    switch (link.wifi_state) {
        case Wifi::kStopped:
        case Wifi::kLoadingCredentials: status_.wifi = Indicator::Off; break;
        case Wifi::kResolving:
        case Wifi::kOpeningTls:
        case Wifi::kTransferring: status_.wifi = Indicator::Connected; break;
        case Wifi::kStopFailed: status_.wifi = Indicator::Fault; break;
        default: status_.wifi = Indicator::Busy; break;
    }
#endif
    ui_.UpdateStatus(status_);
}

void TerminalApp::RunApplicationShell() {
    if (!ComposeApplications()) {
        ESP_LOGE(kTag, "application catalog is full");
        return;
    }
    sdk::ApplicationRuntime runtime(applications_.data(), applications_.size(), "launcher", *this);
    if (!sdk::IsOk(runtime.Start())) return;
    // Retain the boot-evidence marker consumed by the maintenance smoke test.
    LogHeap("M3 runtime active");
    if (!sdk::IsOk(runtime.Step())) return;
    // Confirm a trial image only after platform startup and the first
    // launcher frame have both succeeded.
    const auto confirmed = platform_.Boot().ConfirmBoot();
    if (confirmed != zectrix::update::Result::kOk) {
        ESP_LOGE(kTag, "boot confirmation failed: %s", zectrix::update::ResultName(confirmed));
        return;
    }
    while (runtime.state() == sdk::LifecycleState::Active) {
        sdk::InputEvent event;
        // Pending pagination yields one tick between bounded parse slices.
#if CONFIG_ZECTRIX_ENABLE_READER
        const TickType_t timeout = reader_busy_ ? TickType_t{1} : pdMS_TO_TICKS(250);
#else
        const TickType_t timeout = pdMS_TO_TICKS(250);
#endif
        const bool received = input_->Wait(&event, timeout);
        UpdateSystemStatus();
        const sdk::Status result = received ? runtime.Step(&event) : runtime.Idle();
        if (!sdk::IsOk(result)) {
            ESP_LOGE(kTag, "application step failed: %s", sdk::StatusName(result));
        } else if (runtime.state() == sdk::LifecycleState::Active) {
            const esp_err_t status = ui_.RefreshPending();
            if (status != ESP_OK) ESP_LOGW(kTag, "status refresh failed: %s", esp_err_to_name(status));
        }
    }
}

sdk::Status TerminalApp::Shutdown() {
    PowerOff();
}

void TerminalApp::EnterFailsafe(sdk::Status reason) {
    ESP_LOGE(kTag, "application runtime failsafe: %s",
             sdk::StatusName(reason));
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

ControlResult TerminalApp::Wait(uint32_t duration_ms, bool any_click_returns) {
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
        if (event.button == zectrix::input::Button::Down &&
            event.action == zectrix::input::Action::LongPress) {
            return ControlResult::kShutdown;
        }
        if (event.button == zectrix::input::Button::Ok &&
            event.action == zectrix::input::Action::LongPress) {
            return ControlResult::kBack;
        }
        if (any_click_returns &&
            event.action == zectrix::input::Action::Click) {
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
