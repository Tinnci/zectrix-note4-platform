#include "zectrix_connectivity_settings.h"
#include "zectrix_storage_service.h"

namespace zectrix::connectivity {
esp_err_t StoredConnectivitySettings::LoadPolicy(companion::UserConnectivityPolicy* policy) const {
    if (policy == nullptr) return ESP_ERR_INVALID_ARG;
    *policy = companion::UserConnectivityPolicy::kOffline;
    if (storage_ == nullptr || !storage_->IsInitialized()) return ESP_ERR_INVALID_STATE;
    uint32_t stored = 0;
    const esp_err_t result = storage_->GetUInt32(kConnectivityPolicyKey, &stored);
    if (result == ESP_ERR_NOT_FOUND) {
        *policy = companion::UserConnectivityPolicy::kAutomatic;
        return ESP_OK;
    }
    if (result != ESP_OK) return result;
    if (stored > static_cast<uint32_t>(companion::UserConnectivityPolicy::kOffline)) return ESP_ERR_INVALID_ARG;
    *policy = static_cast<companion::UserConnectivityPolicy>(stored);
    return ESP_OK;
}
esp_err_t StoredConnectivitySettings::SavePolicy(companion::UserConnectivityPolicy policy) {
    if (policy > companion::UserConnectivityPolicy::kOffline) return ESP_ERR_INVALID_ARG;
    if (storage_ == nullptr || !storage_->IsInitialized()) return ESP_ERR_INVALID_STATE;
    uint32_t stored = 0;
    const esp_err_t result = storage_->GetUInt32(kConnectivityPolicyKey, &stored);
    if (result == ESP_OK && stored == static_cast<uint32_t>(policy)) return ESP_OK;
    if (result != ESP_OK && result != ESP_ERR_NOT_FOUND) return result;
    return storage_->SetUInt32(kConnectivityPolicyKey, static_cast<uint32_t>(policy));
}
}  // namespace zectrix::connectivity
