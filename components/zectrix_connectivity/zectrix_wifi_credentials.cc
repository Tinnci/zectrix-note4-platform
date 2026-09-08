#include "zectrix_wifi_credentials.h"

#include "zectrix_storage_service.h"

namespace zectrix::connectivity {
namespace {
constexpr char kStationKey[] = "wifi_station";
static_assert(sizeof(WifiCredentials) == 98);
}  // namespace

WifiCredentialResult StoredWifiCredentials::Load(WifiCredentials* credentials) {
    if (credentials == nullptr) return WifiCredentialResult::kInvalid;
    ClearWifiCredentials(credentials);
    if (storage_ == nullptr || !storage_->IsInitialized()) {
        return WifiCredentialResult::kUnavailable;
    }
    std::size_t size = sizeof(*credentials);
    const esp_err_t result = storage_->GetBlob(kStationKey, credentials, &size);
    if (result == ESP_ERR_NOT_FOUND) return WifiCredentialResult::kUnavailable;
    if (result != ESP_OK || size != sizeof(*credentials) ||
        !ValidateWifiCredentials(*credentials)) {
        ClearWifiCredentials(credentials);
        return WifiCredentialResult::kInvalid;
    }
    return WifiCredentialResult::kAvailable;
}

esp_err_t StoredWifiCredentials::Save(const WifiCredentials& credentials) {
    if (storage_ == nullptr) return ESP_ERR_INVALID_STATE;
    if (!ValidateWifiCredentials(credentials)) return ESP_ERR_INVALID_ARG;
    return storage_->SetBlob(kStationKey, &credentials, sizeof(credentials));
}

esp_err_t StoredWifiCredentials::Erase() {
    if (storage_ == nullptr) return ESP_ERR_INVALID_STATE;
    return storage_->Erase(kStationKey);
}

bool StoredWifiCredentials::Available() {
    WifiCredentials credentials{};
    const bool available = Load(&credentials) == WifiCredentialResult::kAvailable;
    ClearWifiCredentials(&credentials);
    return available;
}

}  // namespace zectrix::connectivity
