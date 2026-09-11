#pragma once

#include "sdkconfig.h"
#include "zectrix_power_service.h"
#include "zectrix_status_bar.h"
#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
#include "zectrix_connectivity_service.h"
#endif

namespace zectrix::terminal {

inline void CopyPowerStatus(ui::StatusBarState& status, const power::PowerSnapshot& power) {
    status.battery_valid = power.battery_valid;
    status.battery_percent = power.battery_percent;
    status.battery_absent = power.battery_absent;
    status.charging = power.charging;
    status.charge_full = power.charge_full;
    status.external_power = power.external_power_present;
    status.charge_fault = power.charge_fault;
}

#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
inline void CopyRadioStatus(ui::StatusBarState& status, const connectivity::ConnectivitySnapshot& link) {
    using Indicator = ui::RadioIndicator;
    using Ble = connectivity::ConnectivityState;
    switch (link.state) {
        case Ble::kStopped: status.ble = Indicator::Off; break;
        case Ble::kIdle:
        case Ble::kAdvertising:
        case Ble::kPairing:
        case Ble::kSecuring: status.ble = Indicator::Ready; break;
        case Ble::kSecure:
        case Ble::kLinkReady:
        case Ble::kProtocolNegotiatedLocal:
            status.ble = link.ble_data_active ? Indicator::Active : Indicator::Connected;
            break;
        case Ble::kFault: status.ble = Indicator::Fault; break;
    }
    using Wifi = connectivity::WifiBackendState;
    switch (link.wifi_state) {
        case Wifi::kStopped:
        case Wifi::kLoadingCredentials: status.wifi = Indicator::Off; break;
        case Wifi::kResolving:
        case Wifi::kOpeningTls:
        case Wifi::kTransferring:
            // A listening hotspot is ready; only an upload shows activity.
            status.wifi = link.wifi_data_active ? Indicator::Active :
                link.wifi.mode == connectivity::WifiMode::AccessPoint ? Indicator::Ready : Indicator::Connected;
            break;
        case Wifi::kStopFailed: status.wifi = Indicator::Fault; break;
        default: status.wifi = Indicator::Ready; break;
    }
}
#endif

}  // namespace zectrix::terminal
