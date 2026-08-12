#include "zectrix_storage_service.h"

#include <new>

#include "nvs.h"
#include "nvs_flash.h"

namespace zectrix::storage {
namespace {

constexpr char kNamespace[] = "zectrix";

bool InvalidKey(const char* key) {
    return key == nullptr || key[0] == '\0';
}

esp_err_t PublicResult(esp_err_t result) {
    return result == ESP_ERR_NVS_NOT_FOUND ? ESP_ERR_NOT_FOUND : result;
}

}  // namespace

struct StorageService::Impl {
    nvs_handle_t handle = 0;
    bool initialized = false;
};

esp_err_t StorageService::Create(StorageService** out_service) {
    if (out_service == nullptr) return ESP_ERR_INVALID_ARG;
    *out_service = nullptr;
    auto* impl = new (std::nothrow) Impl();
    if (impl == nullptr) return ESP_ERR_NO_MEM;
    auto* service = new (std::nothrow) StorageService(impl);
    if (service == nullptr) {
        delete impl;
        return ESP_ERR_NO_MEM;
    }
    *out_service = service;
    return ESP_OK;
}

StorageService::~StorageService() {
    if (impl_ != nullptr) {
        if (impl_->initialized) nvs_close(impl_->handle);
        delete impl_;
    }
}

esp_err_t StorageService::Initialize() {
    if (impl_ == nullptr) return ESP_ERR_INVALID_STATE;
    if (impl_->initialized) return ESP_OK;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err == ESP_OK) err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;
    err = nvs_open(kNamespace, NVS_READWRITE, &impl_->handle);
    if (err == ESP_OK) impl_->initialized = true;
    return PublicResult(err);
}

bool StorageService::IsInitialized() const {
    return impl_ != nullptr && impl_->initialized;
}

esp_err_t StorageService::SetBool(const char* key, bool value) {
    if (!IsInitialized()) return ESP_ERR_INVALID_STATE;
    if (InvalidKey(key)) return ESP_ERR_INVALID_ARG;
    return Commit(nvs_set_u8(impl_->handle, key, value ? 1 : 0));
}

esp_err_t StorageService::GetBool(const char* key, bool* value) const {
    if (!IsInitialized()) return ESP_ERR_INVALID_STATE;
    if (InvalidKey(key) || value == nullptr) return ESP_ERR_INVALID_ARG;
    uint8_t raw = 0;
    const esp_err_t err = nvs_get_u8(impl_->handle, key, &raw);
    if (err == ESP_OK) *value = raw != 0;
    return PublicResult(err);
}

esp_err_t StorageService::SetInt32(const char* key, int32_t value) {
    if (!IsInitialized()) return ESP_ERR_INVALID_STATE;
    if (InvalidKey(key)) return ESP_ERR_INVALID_ARG;
    return Commit(nvs_set_i32(impl_->handle, key, value));
}

esp_err_t StorageService::GetInt32(const char* key, int32_t* value) const {
    if (!IsInitialized()) return ESP_ERR_INVALID_STATE;
    if (InvalidKey(key) || value == nullptr) return ESP_ERR_INVALID_ARG;
    return PublicResult(nvs_get_i32(impl_->handle, key, value));
}

esp_err_t StorageService::SetUInt32(const char* key, uint32_t value) {
    if (!IsInitialized()) return ESP_ERR_INVALID_STATE;
    if (InvalidKey(key)) return ESP_ERR_INVALID_ARG;
    return Commit(nvs_set_u32(impl_->handle, key, value));
}

esp_err_t StorageService::GetUInt32(const char* key, uint32_t* value) const {
    if (!IsInitialized()) return ESP_ERR_INVALID_STATE;
    if (InvalidKey(key) || value == nullptr) return ESP_ERR_INVALID_ARG;
    return PublicResult(nvs_get_u32(impl_->handle, key, value));
}

esp_err_t StorageService::SetString(const char* key, const char* value) {
    if (!IsInitialized()) return ESP_ERR_INVALID_STATE;
    if (InvalidKey(key) || value == nullptr) return ESP_ERR_INVALID_ARG;
    return Commit(nvs_set_str(impl_->handle, key, value));
}

esp_err_t StorageService::GetString(const char* key, char* value,
                                    std::size_t* length) const {
    if (!IsInitialized()) return ESP_ERR_INVALID_STATE;
    if (InvalidKey(key) || length == nullptr) return ESP_ERR_INVALID_ARG;
    return PublicResult(nvs_get_str(impl_->handle, key, value, length));
}

esp_err_t StorageService::SetBlob(const char* key, const void* value,
                                  std::size_t length) {
    if (!IsInitialized()) return ESP_ERR_INVALID_STATE;
    if (InvalidKey(key) || (value == nullptr && length != 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    return Commit(nvs_set_blob(impl_->handle, key, value, length));
}

esp_err_t StorageService::GetBlob(const char* key, void* value,
                                  std::size_t* length) const {
    if (!IsInitialized()) return ESP_ERR_INVALID_STATE;
    if (InvalidKey(key) || length == nullptr) return ESP_ERR_INVALID_ARG;
    return PublicResult(nvs_get_blob(impl_->handle, key, value, length));
}

esp_err_t StorageService::Erase(const char* key) {
    if (!IsInitialized()) return ESP_ERR_INVALID_STATE;
    if (InvalidKey(key)) return ESP_ERR_INVALID_ARG;
    return PublicResult(Commit(nvs_erase_key(impl_->handle, key)));
}

esp_err_t StorageService::Commit(esp_err_t operation_result) {
    if (operation_result != ESP_OK) return operation_result;
    return nvs_commit(impl_->handle);
}

}  // namespace zectrix::storage
