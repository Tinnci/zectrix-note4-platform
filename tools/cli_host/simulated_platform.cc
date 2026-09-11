#include "simulated_platform.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace zectrix::cli::host {
namespace {
thread_local const SimulatedPlatform* current_owner = nullptr;
}

SimulatedPlatform::SimulatedPlatform(LogBuffer& logs, SimulationOptions options)
    : logs_(logs), options_(options) {
    auto& system = snapshot_.system;
    std::snprintf(system.firmware.project_name.data(), system.firmware.project_name.size(), "zectrix-host-sim");
    std::snprintf(system.firmware.version.data(), system.firmware.version.size(), "D1.4-host");
    std::snprintf(system.firmware.idf_version.data(), system.firmware.idf_version.size(), "simulated");
    std::snprintf(system.capabilities.chip_model.data(), system.capabilities.chip_model.size(), "SIMULATED-ESP32-S3");
    system.capabilities.core_count = 2;
    system.capabilities.wifi = system.capabilities.bluetooth_le = true;
    system.capabilities.rtc = system.capabilities.nfc = system.capabilities.psram = true;
    system.reset_reason = system::ResetReason::PowerOn;
    system.wifi_mac = {0x02, 0, 0, 0, 0, 1};
    system.diagnostics.flash_bytes = 16 * 1024 * 1024;
    system.diagnostics.psram_bytes = 8 * 1024 * 1024;
    snapshot_.heap.internal = {320 * 1024, 240 * 1024, 200 * 1024, 128 * 1024};
    snapshot_.heap.psram = {8 * 1024 * 1024, 7 * 1024 * 1024, 6 * 1024 * 1024, 4 * 1024 * 1024};
    snapshot_.tasks.total = snapshot_.tasks.count = 2;
    snapshot_.tasks.tasks[0] = {1, 1, 2048, system::TaskState::kRunning, true};
    snapshot_.tasks.tasks[1] = {2, 3, 3072, system::TaskState::kReady, false};
    snapshot_.display.bits_per_pixel = 1;
    snapshot_.display.framebuffer_bytes = 15000;
    snapshot_.display.framebuffer_valid = true;
    snapshot_.power = {0, 3920, 78, true, false, false, false, false, false};
    snapshot_.time.offset_seconds = 28800;
    snapshot_.time.offset_known = snapshot_.time.rtc_available = snapshot_.time.persisted = true;
    snapshot_.time.sync.source = time::SyncSource::Rtc;
    auto& c = snapshot_.connectivity;
    std::strcpy(c.radio.data(), "companion");
    std::strcpy(c.wifi.data(), "stopped");
    std::strcpy(c.ble.data(), "advertising");
    snapshot_.apps.count = 2;
    snapshot_.apps.generation = 1;
    snapshot_.apps.lifecycle = 3;
    std::strcpy(snapshot_.apps.foreground.data(), "launcher");
    std::strcpy(snapshot_.apps.entries[0].id.data(), "launcher");
    std::strcpy(snapshot_.apps.entries[0].label.data(), "HOME");
    std::strcpy(snapshot_.apps.entries[1].id.data(), "clock");
    std::strcpy(snapshot_.apps.entries[1].label.data(), "CLOCK");
    snapshot_.scenes.depth = 1;
    snapshot_.scenes.views[0] = {0, 0, 400, 24, true, true, false, false};
    snapshot_.scenes.views[1] = {0, 24, 400, 276, true, true, false, false};
}

void SimulatedPlatform::Start(PlatformControlDispatcher& dispatcher) {
    thread_ = std::thread([this, &dispatcher] { Run(dispatcher); });
}

void SimulatedPlatform::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        changed_.notify_all();
    }
    if (thread_.joinable()) thread_.join();
}

bool SimulatedPlatform::IsCurrentTaskOwner() const { return current_owner == this; }

void SimulatedPlatform::Wake() {
    std::lock_guard<std::mutex> lock(mutex_);
    wake_pending_ = true;
    changed_.notify_one();
}

ControlStatus SimulatedPlatform::Inspect(const ControlRequest& request, ControlResult* result) {
    if (!IsCurrentTaskOwner()) return ControlStatus::kDenied;
    if (result == nullptr) return ControlStatus::kInvalidArgument;
    snapshot_.uptime_us = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - started_).count();
    if (IsMutation(request.operation)) {
        if (!request.confirmed || request.origin != Origin::kUsbLocal) return ControlStatus::kDenied;
        if (request.operation == ControlOperation::kTimeSync) {
            const auto local = static_cast<time_t>(request.unix_ms / 1000 + request.offset_seconds);
            tm date{};
            if (!gmtime_r(&local, &date) || date.tm_year < 100 || date.tm_year > 199)
                return ControlStatus::kInvalidArgument;
            unix_base_ms_ = request.unix_ms - static_cast<int64_t>(snapshot_.uptime_us / 1000);
            snapshot_.time.offset_seconds = request.offset_seconds;
            snapshot_.time.sync.source = time::SyncSource::Manual;
            snapshot_.time.sync.result = time::SyncResult::Applied;
        } else {
            logs_.Push(LogLevel::kInfo, "I host: maintenance action simulated; host files and power are unchanged");
        }
    }
    snapshot_.time.unix_seconds = (unix_base_ms_ + snapshot_.uptime_us / 1000) / 1000;
    const auto local = static_cast<time_t>(snapshot_.time.unix_seconds + snapshot_.time.offset_seconds);
    tm date{};
    snapshot_.time.calendar_valid = gmtime_r(&local, &date) != nullptr;
    snapshot_.time.local = {static_cast<int16_t>(date.tm_year + 1900), static_cast<int16_t>(date.tm_mon + 1),
        static_cast<int16_t>(date.tm_mday), static_cast<int16_t>(date.tm_hour),
        static_cast<int16_t>(date.tm_min), static_cast<int16_t>(date.tm_sec)};
    *result = snapshot_;
    if (request.operation == ControlOperation::kInput) result->input = input_trace_.Read(request.cursor);
    return ControlStatus::kOk;
}

void SimulatedPlatform::Refresh() {
    auto& frame = snapshot_.display;
    constexpr uint32_t changed_pixels = 16 * 8;
    if (!display_state_.CanUsePartial() || display_state_.ShouldRequestFullClean(changed_pixels)) {
        display_state_.OnFull1BppSuccess();
        frame.last_refresh = display::RefreshKind::kFull1Bpp;
        frame.last_duration_us = 250000;
    } else {
        display_state_.OnPartial1BppSuccess({0, 0, 16, 8}, changed_pixels);
        frame.last_refresh = display::RefreshKind::kPartial1Bpp;
        frame.last_duration_us = 50000;
    }
    frame.state = display_state_.state();
    ++frame.refresh_count;
    input_trace_.Push(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started_).count(),
        static_cast<uint8_t>(frame.refresh_count % 3), 0, true);
    for (std::size_t index = 0; index < frame.preview.size(); ++index) {
        frame.preview[index] = static_cast<uint8_t>(frame.refresh_count + index);
    }
}

void SimulatedPlatform::EmitLog() {
    ++log_sequence_;
    const auto level = log_sequence_ % 5 == 0 ? LogLevel::kWarn : LogLevel::kInfo;
    char text[128];
    std::snprintf(text, sizeof(text), "%c host: simulated event=%lu refresh=%lu",
                  level == LogLevel::kWarn ? 'W' : 'I',
                  static_cast<unsigned long>(log_sequence_),
                  static_cast<unsigned long>(snapshot_.display.refresh_count));
    logs_.Push(level, text);
}

void SimulatedPlatform::Run(PlatformControlDispatcher& dispatcher) {
    current_owner = this;
    Refresh();
    for (uint32_t index = 0; index < options_.log_burst; ++index) EmitLog();
    auto next_frame = Clock::now() + std::chrono::milliseconds(250);
    auto next_log = options_.log_interval_ms == 0 ? Clock::time_point::max()
        : Clock::now() + std::chrono::milliseconds(options_.log_interval_ms);
    auto dispatch_at = Clock::time_point::max();
    bool retry_dispatch = false;
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            changed_.wait_until(lock, std::min({next_frame, next_log, dispatch_at}),
                                [this] { return wake_pending_ || stopping_; });
            if (stopping_) break;
            if (wake_pending_) {
                wake_pending_ = false;
                retry_dispatch = false;
                dispatch_at = Clock::now() + std::chrono::milliseconds(options_.owner_delay_ms);
            }
        }
        const auto now = Clock::now();
        if (now >= next_frame) {
            Refresh();
            // Like the device's input loop, retry owner safe points if a
            // nonblocking dispatch collided with the CLI copying a result.
            if (retry_dispatch) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!wake_pending_) retry_dispatch = !dispatcher.Dispatch();
            }
            next_frame = now + std::chrono::milliseconds(250);
        }
        if (now >= next_log) {
            EmitLog();
            next_log = now + std::chrono::milliseconds(options_.log_interval_ms);
        }
        if (now >= dispatch_at) {
            retry_dispatch = !dispatcher.Dispatch();
            dispatch_at = Clock::time_point::max();
        }
    }
    current_owner = nullptr;
}

}  // namespace zectrix::cli::host
