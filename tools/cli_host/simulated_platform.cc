#include "simulated_platform.h"

#include <algorithm>
#include <cstdio>

namespace zectrix::cli::host {
namespace {
thread_local const SimulatedPlatform* current_owner = nullptr;
}

SimulatedPlatform::SimulatedPlatform(LogBuffer& logs, SimulationOptions options)
    : logs_(logs), options_(options) {
    auto& system = snapshot_.system;
    std::snprintf(system.firmware.project_name.data(), system.firmware.project_name.size(), "zectrix-host-sim");
    std::snprintf(system.firmware.version.data(), system.firmware.version.size(), "D1.3-host");
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

ControlStatus SimulatedPlatform::Inspect(const ControlRequest&, ControlResult* result) {
    if (!IsCurrentTaskOwner()) return ControlStatus::kDenied;
    if (result == nullptr) return ControlStatus::kInvalidArgument;
    snapshot_.uptime_us = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - started_).count();
    *result = snapshot_;
    return ControlStatus::kOk;
}

void SimulatedPlatform::Refresh() {
    auto& frame = snapshot_.display;
    if (!display_state_.CanUsePartial() || display_state_.ShouldRequestFullClean()) {
        display_state_.OnFull1BppSuccess();
        frame.last_refresh = display::RefreshKind::kFull1Bpp;
        frame.last_duration_us = 250000;
    } else {
        display_state_.OnPartial1BppSuccess({0, 0, 16, 8});
        frame.last_refresh = display::RefreshKind::kPartial1Bpp;
        frame.last_duration_us = 50000;
    }
    frame.state = display_state_.state();
    ++frame.refresh_count;
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
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            changed_.wait_until(lock, std::min({next_frame, next_log, dispatch_at}),
                                [this] { return wake_pending_ || stopping_; });
            if (stopping_) break;
            if (wake_pending_) {
                wake_pending_ = false;
                dispatch_at = Clock::now() + std::chrono::milliseconds(options_.owner_delay_ms);
            }
        }
        const auto now = Clock::now();
        if (now >= next_frame) {
            Refresh();
            next_frame = now + std::chrono::milliseconds(250);
        }
        if (now >= next_log) {
            EmitLog();
            next_log = now + std::chrono::milliseconds(options_.log_interval_ms);
        }
        if (now >= dispatch_at) {
            dispatcher.Dispatch();
            dispatch_at = Clock::time_point::max();
        }
    }
    current_owner = nullptr;
}

}  // namespace zectrix::cli::host
