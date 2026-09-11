#include "zectrix_platform_diagnostics.h"

#include "zectrix_display_service.h"
#include "zectrix_input_service.h"
#include "zectrix_system_service.h"
#include "zectrix_time_service.h"
#include "zectrix_power_service.h"
#include "zectrix_service_registry.h"
#include "sdkconfig.h"
#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
#include "zectrix_connectivity_service.h"
#endif

#include <algorithm>
#include <cstdio>

namespace zectrix {

PlatformDiagnostics::PlatformDiagnostics(const ServiceRegistry& services, system::SystemService& system,
                                         display::DisplayService& display,
                                         input::InputService& input,
                                         time::TimeService& time,
                                         cli::CliBinarySession* binary)
    : services_(services), system_(system), display_(display), input_(input), time_(time),
      owner_task_(xTaskGetCurrentTaskHandle()), dispatcher_(*this),
      executor_(dispatcher_, cli::MaintenanceLogs(), binary) {
    input_.SetWaitHook(OnWait, this);
}

PlatformDiagnostics::~PlatformDiagnostics() {
    Shutdown();
    input_.SetWaitHook(nullptr, nullptr);
}

void PlatformDiagnostics::OnWait(void* context) {
    static_cast<PlatformDiagnostics*>(context)->Poll();
}

bool PlatformDiagnostics::IsCurrentTaskOwner() const {
    return xTaskGetCurrentTaskHandle() == owner_task_;
}

void PlatformDiagnostics::Wake() { input_.WakeWait(); }
void PlatformDiagnostics::Poll() { dispatcher_.Dispatch(); }
void PlatformDiagnostics::Shutdown() { dispatcher_.Shutdown(); }

void PlatformDiagnostics::ReadTime(cli::TimeInspection* result) const {
    const auto clock = time_.Now();
    const auto state = time_.Status();
    const auto& d = clock.value;
    result->calendar_valid = clock.source != time::ClockSource::Uptime;
    if (result->calendar_valid) result->local = {static_cast<int16_t>(d.year), static_cast<int16_t>(d.month),
        static_cast<int16_t>(d.day), static_cast<int16_t>(d.hour), static_cast<int16_t>(d.minute), static_cast<int16_t>(d.second)};
    result->unix_seconds = time_.UnixSeconds();
    result->offset_seconds = state.utc_offset_seconds;
    result->offset_known = state.utc_offset_known;
    result->persisted = state.rtc_persisted;
    result->pending = state.persistence_pending;
    result->error = state.last_error;
    result->rtc_available = time_.RtcAvailable();
    result->sync = time_.Synchronization();
    result->accepted_age_ms = result->sync.source == time::SyncSource::None ? -1 :
        (time_.MonotonicMicroseconds() - result->sync.accepted_us) / 1000;
}

cli::ControlStatus PlatformDiagnostics::Inspect(const cli::ControlRequest& request,
                                                cli::ControlResult* result) {
    if (!IsCurrentTaskOwner()) return cli::ControlStatus::kDenied;
    if (!result) return cli::ControlStatus::kInvalidArgument;
    if (cli::IsMutation(request.operation) && (!request.confirmed || request.origin != cli::Origin::kUsbLocal))
        return cli::ControlStatus::kDenied;
    esp_err_t err = ESP_ERR_INVALID_ARG;
    switch (request.operation) {
        case cli::ControlOperation::kSystemInfo:
            err = system_.ReadSnapshot(&result->system);
            break;
        case cli::ControlOperation::kHeap:
            err = system_.ReadHeap(&result->heap);
            break;
        case cli::ControlOperation::kTasks:
            err = system_.ReadTasks(&result->tasks);
            break;
        case cli::ControlOperation::kUptime:
            result->uptime_us = time_.MonotonicMicroseconds();
            err = ESP_OK;
            break;
        case cli::ControlOperation::kDisplay:
            err = display_.ReadInspection(&result->display);
            break;
        case cli::ControlOperation::kPower: {
            auto* power = services_.Get<power::PowerService>();
            power::PowerSnapshot p;
            int64_t sampled = 0;
            if (!power || !power->CachedSnapshot(&p, &sampled)) return cli::ControlStatus::kUnavailable;
            result->power = {(time_.MonotonicMicroseconds() - sampled) / 1000, p.battery_mv, p.battery_percent,
                p.battery_valid, p.external_power_present, p.charging, p.charge_full, p.charge_fault, p.battery_absent};
            err = ESP_OK;
            break;
        }
        case cli::ControlOperation::kTimeSync:
            err = time_.SetUnixTime(request.unix_ms, request.offset_seconds);
            if (err != ESP_OK) break;
            [[fallthrough]];
        case cli::ControlOperation::kTime:
            ReadTime(&result->time);
            err = ESP_OK;
            break;
        case cli::ControlOperation::kInput:
            result->input = input_.ReadTrace(request.cursor);
            err = ESP_OK;
            break;
        case cli::ControlOperation::kApps:
        case cli::ControlOperation::kScenes:
            return delegate_ ? delegate_->InspectApps(result) : cli::ControlStatus::kUnavailable;
        case cli::ControlOperation::kReboot:
        case cli::ControlOperation::kSleep:
        case cli::ControlOperation::kStorageWipe:
        case cli::ControlOperation::kFactoryReset:
#if !CONFIG_ZECTRIX_ENABLE_BOOK_STORAGE
            if (request.operation == cli::ControlOperation::kStorageWipe) return cli::ControlStatus::kUnavailable;
#endif
            return delegate_ ? delegate_->ScheduleMaintenance(request.operation) : cli::ControlStatus::kUnavailable;
        case cli::ControlOperation::kConnectivity: {
#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
            const auto* service = services_.Get<connectivity::ConnectivityService>();
            if (!service) return cli::ControlStatus::kUnavailable;
            connectivity::ConnectivitySnapshot state;
            if (!service->TrySnapshot(&state)) return cli::ControlStatus::kBusy;
            auto& c = result->connectivity;
            constexpr const char* ble[] = {"stopped", "idle", "advertising", "pairing", "securing", "secure", "link-ready", "negotiated-local", "fault"};
            constexpr const char* wifi[] = {"stopped", "loading-credentials", "starting-station", "associating", "waiting-ip", "resolving", "opening-tls", "transferring", "stopping", "stop-failed"};
            constexpr const char* radio[] = {"companion", "wifi-burst", "shared-idle", "wifi-stopping"};
            const auto copy = [](auto& target, const char* value) { std::snprintf(target.data(), target.size(), "%s", value); };
            const auto name = [](auto value, const auto& names) {
                const auto index = static_cast<std::size_t>(value);
                return index < std::size(names) ? names[index] : "unknown";
            };
            copy(c.ble, name(state.state, ble));
            copy(c.wifi, name(state.wifi_state, wifi));
            copy(c.radio, name(state.radio_mode, radio));
            c.session = state.session_id;
            c.encrypted = state.encrypted; c.authenticated = state.authenticated; c.bonded = state.bonded;
            c.authorized = state.peer_authorized; c.negotiated = state.protocol_negotiated_local;
            c.pairing = state.local_pairing_active; c.transfer = state.book_transfer_active; c.busy = state.resource_busy;
            c.mode = static_cast<uint8_t>(state.wifi.mode);
            c.ssid = state.wifi.ssid;
            // SSIDs are external bytes; never let terminal control codes escape.
            for (char& value : c.ssid) if (value && (static_cast<uint8_t>(value) < 0x20 || static_cast<uint8_t>(value) >= 0x7f)) value = '?';
            c.address = state.wifi.address; c.mac = state.wifi.mac; c.rssi = state.wifi.rssi;
            c.rssi_valid = state.wifi.rssi_valid; c.mac_valid = state.wifi.mac_valid;
            err = ESP_OK;
#else
            return cli::ControlStatus::kUnavailable;
#endif
            break;
        }
    }
    if (err == ESP_OK) return cli::ControlStatus::kOk;
    return err == ESP_ERR_INVALID_ARG ? cli::ControlStatus::kInvalidArgument
                                      : cli::ControlStatus::kUnavailable;
}

}  // namespace zectrix
