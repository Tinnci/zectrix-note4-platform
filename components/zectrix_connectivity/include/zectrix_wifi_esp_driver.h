#pragma once

#include "zectrix_wifi_backend.h"

namespace zectrix::connectivity {

struct WifiScanSnapshot {
    uint16_t access_point_count = 0;
    int8_t best_rssi = -127;
    std::array<char, kMaximumWifiSsidBytes + 1> best_ssid{};
    bool target_found = false;
};

// Both resource transfers and RF diagnostics use this exclusive radio owner.
// Methods run on the caller's owner task; callbacks only publish driver state.
class EspWifiBackendDriver final : public WifiBackendDriver {
public:
    EspWifiBackendDriver();
    ~EspWifiBackendDriver() override;
    EspWifiBackendDriver(const EspWifiBackendDriver&) = delete;
    EspWifiBackendDriver& operator=(const EspWifiBackendDriver&) = delete;

    WifiDriverResult StartStation(const WifiCredentials& credentials) override;
    WifiDriverResult PollAssociation() override;
    WifiDriverResult PollIp() override;
    WifiDriverResult Resolve(companion::ResourceCapability capability) override;
    WifiDriverResult OpenTls(companion::ResourceCapability capability) override;
    WifiDriverResult Fetch(companion::ResourceCapability capability,
                           uint8_t* body, std::size_t capacity,
                           std::size_t* body_size) override;
    WifiDriverResult StopStation() override;

    // An empty target scans all APs. A non-empty target qualifies that SSID.
    WifiDriverResult StartScan();
    WifiDriverResult PollScan(const char* target, WifiScanSnapshot* snapshot);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace zectrix::connectivity
