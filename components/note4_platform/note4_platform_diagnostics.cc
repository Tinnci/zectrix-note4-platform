#include "note4_platform_diagnostics.h"
#include "note4_display_calibration_store.h"

#include "note4_display_service.h"
#include "note4_input_service.h"
#include "note4_system_service.h"
#include "note4_health_supervisor.h"
#include "note4_time_service.h"
#include "note4_power_service.h"
#include "note4_service_registry.h"
#include "sdkconfig.h"
#if CONFIG_NOTE4_ENABLE_CONNECTIVITY
#include "note4_connectivity_service.h"
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace note4 {

PlatformDiagnostics::PlatformDiagnostics(const ServiceRegistry& services,
                                         system::SystemService& system,
                                         display::DisplayService& display,
                                         input::InputService& input, time::TimeService& time,
                                         system::HealthSupervisor& health,
                                         cli::CliBinarySession* binary)
    : services_(services), system_(system), display_(display), input_(input), time_(time),
      health_(health), owner_task_(xTaskGetCurrentTaskHandle()), dispatcher_(*this),
      executor_(dispatcher_, cli::MaintenanceLogs(), binary, cli::SteadyMilliseconds,
                &log::EspLevelControl()) {
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
        case cli::ControlOperation::kHealth:
            result->health = health_.Snapshot();
            err = ESP_OK;
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
        case cli::ControlOperation::kDisplaySettings:
        case cli::ControlOperation::kDisplayConfigure:
            return delegate_ ? delegate_->HandleDisplaySettings(request, result) : cli::ControlStatus::kUnavailable;
        case cli::ControlOperation::kDisplayTelemetry:
            result->display_telemetry = display_.ReadTelemetry(request.cursor);
            err = ESP_OK;
            break;
        case cli::ControlOperation::kDisplayModel:
        case cli::ControlOperation::kDisplayModelSet:
        case cli::ControlOperation::kDisplayModelReset: {
            auto* storage = services_.Get<storage::StorageService>();
            if (!storage)
                return cli::ControlStatus::kUnavailable;
            result->display_model = display_.physics_parameters();
            if (request.operation == cli::ControlOperation::kDisplayModelReset) {
                err = display::ResetModel(*storage);
                if (err != ESP_OK)
                    break;
            }
            result->model_storage_error =
                display::LoadModel(*storage, &result->saved_model, &result->model_saved);
            if (request.operation == cli::ControlOperation::kDisplayModelSet) {
                if (result->model_storage_error != ESP_OK) {
                    err = result->model_storage_error;
                    break;
                }
                if (!display::UpdateModel(&result->saved_model, request.text1.data(),
                                          request.values.data(), request.cursor))
                    return cli::ControlStatus::kInvalidArgument;
                err = display::SaveModel(*storage, result->saved_model);
                if (err != ESP_OK)
                    break;
                result->model_saved = true;
            }
            err = ESP_OK;
            break;
        }
        case cli::ControlOperation::kDisplayCalibration:
        case cli::ControlOperation::kDisplayCalibrationSet:
        case cli::ControlOperation::kDisplayCalibrationReset: {
            auto* storage = services_.Get<storage::StorageService>();
            if (!storage)
                return cli::ControlStatus::kUnavailable;
            auto& c = result->calibration;
            err = display_.ReadCalibration(&c.active);
            if (err != ESP_OK)
                break;
            if (request.operation == cli::ControlOperation::kDisplayCalibrationReset) {
                err = display::ResetCalibration(*storage);
                if (err != ESP_OK)
                    break;
            }
            c.storage_error = display::LoadCalibration(*storage, &c.configured, &c.saved);
            if (request.operation == cli::ControlOperation::kDisplayCalibrationSet) {
                // Read errors must not silently replace preserved calibration.
                if (c.storage_error != ESP_OK) {
                    err = c.storage_error;
                    break;
                }
                const auto& v = request.values;
                if (v[0] >= 16 || v[1] > 5 || v[2] > 5 || v[3] > 127 || v[4] > 1000 || v[5] > 1)
                    return cli::ControlStatus::kInvalidArgument;
                const note4_epd_gray_level_t level{
                    static_cast<uint8_t>(v[1]), static_cast<uint8_t>(v[2]),
                    static_cast<uint8_t>(v[3]), static_cast<uint16_t>(v[4])};
                if (!note4_epd_calibration_update(&c.configured, v[0], &level, v[5]))
                    return cli::ControlStatus::kInvalidArgument;
                err = display::SaveCalibration(*storage, c.configured);
                if (err != ESP_OK)
                    break;
                c.saved = true;
            }
            err = ESP_OK; // Read-only reports fallback AND the storage error.
            break;
        }
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
#if !CONFIG_NOTE4_ENABLE_BOOK_STORAGE
            if (request.operation == cli::ControlOperation::kStorageWipe) return cli::ControlStatus::kUnavailable;
#endif
            return delegate_ ? delegate_->ScheduleMaintenance(request.operation) : cli::ControlStatus::kUnavailable;
        case cli::ControlOperation::kConnectivityConfigure: {
#if CONFIG_NOTE4_ENABLE_CONNECTIVITY
            auto* service = services_.Get<connectivity::ConnectivityService>();
            if (!service) return cli::ControlStatus::kUnavailable;
            using Config = cli::ControlRequest::Config;
            if (!std::memchr(request.text1.data(), 0, request.text1.size()) ||
                !std::memchr(request.text2.data(), 0, request.text2.size())) return cli::ControlStatus::kInvalidArgument;
            if (request.config == Config::EdgeToken) {
                err = service->ConfigureEdgeToken(request.text1.data());
                break;
            }
            if (request.config == Config::Policy) {
                if (request.values[0] > 3) return cli::ControlStatus::kInvalidArgument;
                const auto set = service->SetUserPolicy(static_cast<companion::UserConnectivityPolicy>(request.values[0]));
                return set == connectivity::ConnectivityResult::kOk ? cli::ControlStatus::kOk : cli::ControlStatus::kUnavailable;
            }
            if (request.config == Config::WifiSet || request.config == Config::WifiClear) {
                connectivity::WifiCredentials credentials{};
                if (request.config == Config::WifiSet) {
                    if (std::strlen(request.text1.data()) > 32) return cli::ControlStatus::kInvalidArgument;
                    std::strcpy(credentials.ssid.data(), request.text1.data());
                    std::strcpy(credentials.passphrase.data(), request.text2.data());
                    if (!connectivity::ValidateWifiCredentials(credentials)) {
                        connectivity::ClearWifiCredentials(&credentials);
                        return cli::ControlStatus::kInvalidArgument;
                    }
                }
                const auto set = request.config == Config::WifiSet ? service->ConfigureWifi(credentials) : service->ClearWifiConfiguration();
                connectivity::ClearWifiCredentials(&credentials);
                return set == connectivity::ConnectivityResult::kOk ? cli::ControlStatus::kOk :
                    set == connectivity::ConnectivityResult::kBusy ? cli::ControlStatus::kBusy : cli::ControlStatus::kUnavailable;
            }
            connectivity::EdgeSettings settings;
            err = service->LoadEdgeSettings(&settings);
            if (err != ESP_OK) break;
            if (request.config == Config::EdgeSource) {
                if (std::strlen(request.text1.data()) >= 64 || std::strlen(request.text2.data()) >= 64) return cli::ControlStatus::kInvalidArgument;
                std::strcpy(settings.host.data(), request.text1.data()); std::strcpy(settings.path.data(), request.text2.data());
            } else if (request.config == Config::EdgeDisplay) {
                if (request.values[0] > 1) return cli::ControlStatus::kInvalidArgument;
                settings.show_page = request.values[0];
            } else if (request.config == Config::EdgeTelemetry) {
                if (request.values[0] > 1) return cli::ControlStatus::kInvalidArgument;
                settings.bthome_enabled = request.values[0];
            } else if (request.config == Config::EdgeSync) {
                if (request.values[0] > 1 || request.values[3] > 100 || request.values[4] >= 1440 || request.values[5] >= 1440) return cli::ControlStatus::kInvalidArgument;
                settings.enabled = request.values[0]; settings.interval_seconds = request.values[1]; settings.budget_ms = request.values[2];
                settings.minimum_battery_percent = request.values[3]; settings.quiet_start_minute = request.values[4]; settings.quiet_end_minute = request.values[5];
            } else return cli::ControlStatus::kInvalidArgument;
            err = service->ConfigureEdgeSettings(settings);
#else
            return cli::ControlStatus::kUnavailable;
#endif
            break;
        }
        case cli::ControlOperation::kConnectivity: {
#if CONFIG_NOTE4_ENABLE_CONNECTIVITY
            const auto* service = services_.Get<connectivity::ConnectivityService>();
            if (!service) return cli::ControlStatus::kUnavailable;
            connectivity::ConnectivitySnapshot state;
            if (!service->TrySnapshot(&state)) return cli::ControlStatus::kBusy;
            auto& c = result->connectivity;
            c.policy = static_cast<uint8_t>(state.user_policy);
            c.credentials = state.wifi_credentials_available;
            connectivity::EdgeSettings settings;
            c.config_valid = service->LoadEdgeSettings(&settings) == ESP_OK;
            c.background = settings.enabled; c.remote_display = settings.show_page;
            c.bthome = settings.bthome_enabled;
            c.interval_seconds = settings.interval_seconds; c.budget_ms = settings.budget_ms;
            c.minimum_battery = settings.minimum_battery_percent;
            c.quiet_start = settings.quiet_start_minute; c.quiet_end = settings.quiet_end_minute;
            c.source_host = settings.host; c.source_path = settings.path;
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

}  // namespace note4
