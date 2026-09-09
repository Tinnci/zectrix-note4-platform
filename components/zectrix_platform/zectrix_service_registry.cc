#include "zectrix_service_registry.h"

namespace zectrix {

esp_err_t ServiceRegistry::Add(const void* key, Service& service, void* (*get)(Service&)) {
    if (phase_ != Phase::Configuring) return ESP_ERR_INVALID_STATE;
    for (std::size_t i = 0; i < count_; ++i)
        if (entries_[i].key == key || entries_[i].service == &service) return ESP_ERR_INVALID_STATE;
    if (count_ == entries_.size()) return ESP_ERR_NO_MEM;
    entries_[count_++] = {key, &service, get, false, false};
    return ESP_OK;
}

void* ServiceRegistry::Find(const void* key) const {
    for (std::size_t i = 0; i < count_; ++i) {
        const auto& entry = entries_[i];
        if (entry.key == key && entry.running) return entry.get(*entry.service);
    }
    return nullptr;
}

esp_err_t ServiceRegistry::StartAll() {
    if (phase_ == Phase::Running) return ESP_OK;
    if (phase_ != Phase::Configuring) return ESP_ERR_INVALID_STATE;
    phase_ = Phase::Starting;
    for (std::size_t i = 0; i < count_; ++i) {
        auto& entry = entries_[i];
        entry.entered = true;
        esp_err_t result = entry.service->Init();
        if (result == ESP_OK) result = entry.service->Start();
        if (result == ESP_OK && entry.get(*entry.service) == nullptr) result = ESP_ERR_INVALID_STATE;
        if (result != ESP_OK) {
            StopEntered();
            return result;
        }
        entry.running = true;
    }
    phase_ = Phase::Running;
    return ESP_OK;
}

esp_err_t ServiceRegistry::StopEntered() {
    phase_ = Phase::Stopping;
    esp_err_t result = ESP_OK;
    for (std::size_t i = count_; i > 0; --i) {
        auto& entry = entries_[i - 1];
        if (!entry.entered) continue;
        // Withdraw the interface before its callback can release the object.
        entry.running = entry.entered = false;
        const esp_err_t stopped = entry.service->Stop();
        if (result == ESP_OK) result = stopped;
    }
    phase_ = Phase::Stopped;
    return result;
}

esp_err_t ServiceRegistry::StopAll() {
    if (phase_ == Phase::Starting || phase_ == Phase::Stopping) return ESP_ERR_INVALID_STATE;
    return StopEntered();
}

esp_err_t ServiceRegistry::Clear() {
    if (phase_ != Phase::Configuring && phase_ != Phase::Stopped) return ESP_ERR_INVALID_STATE;
    entries_ = {};
    count_ = 0;
    phase_ = Phase::Configuring;
    return ESP_OK;
}

}  // namespace zectrix
