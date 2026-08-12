#include "zectrix_storage_service.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <cassert>
#include <cstring>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

using Value = std::variant<std::uint8_t, std::int32_t, std::uint32_t,
                           std::string, std::vector<std::uint8_t>>;
static std::unordered_map<std::string, Value> values;
static esp_err_t next_init_result = ESP_OK;
static int erase_count = 0;
static int commit_count = 0;

esp_err_t nvs_flash_init() {
    const esp_err_t result = next_init_result;
    next_init_result = ESP_OK;
    return result;
}
esp_err_t nvs_flash_erase() { ++erase_count; values.clear(); return ESP_OK; }
esp_err_t nvs_open(const char* name, nvs_open_mode_t, nvs_handle_t* handle) {
    if (std::string(name) != "zectrix") return ESP_FAIL;
    *handle = 1;
    return ESP_OK;
}
void nvs_close(nvs_handle_t) {}
esp_err_t nvs_commit(nvs_handle_t) { ++commit_count; return ESP_OK; }

template <typename T>
esp_err_t Set(const char* key, T value) { values[key] = std::move(value); return ESP_OK; }
template <typename T>
esp_err_t Get(const char* key, T* value) {
    const auto it = values.find(key);
    if (it == values.end() || !std::holds_alternative<T>(it->second)) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *value = std::get<T>(it->second);
    return ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t, const char* key, std::uint8_t value) { return Set(key, value); }
esp_err_t nvs_get_u8(nvs_handle_t, const char* key, std::uint8_t* value) { return Get(key, value); }
esp_err_t nvs_set_i32(nvs_handle_t, const char* key, std::int32_t value) { return Set(key, value); }
esp_err_t nvs_get_i32(nvs_handle_t, const char* key, std::int32_t* value) { return Get(key, value); }
esp_err_t nvs_set_u32(nvs_handle_t, const char* key, std::uint32_t value) { return Set(key, value); }
esp_err_t nvs_get_u32(nvs_handle_t, const char* key, std::uint32_t* value) { return Get(key, value); }
esp_err_t nvs_set_str(nvs_handle_t, const char* key, const char* value) { return Set(key, std::string(value)); }
esp_err_t nvs_get_str(nvs_handle_t, const char* key, char* value, std::size_t* length) {
    std::string stored;
    const esp_err_t err = Get(key, &stored);
    if (err != ESP_OK) return err;
    const std::size_t required = stored.size() + 1;
    if (value == nullptr) { *length = required; return ESP_OK; }
    if (*length < required) { *length = required; return ESP_ERR_NVS_INVALID_LENGTH; }
    std::memcpy(value, stored.c_str(), required); *length = required; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t, const char* key, const void* value, std::size_t length) {
    const auto* bytes = static_cast<const std::uint8_t*>(value);
    return Set(key, std::vector<std::uint8_t>(bytes, bytes + length));
}
esp_err_t nvs_get_blob(nvs_handle_t, const char* key, void* value, std::size_t* length) {
    std::vector<std::uint8_t> stored;
    const esp_err_t err = Get(key, &stored);
    if (err != ESP_OK) return err;
    if (value == nullptr) { *length = stored.size(); return ESP_OK; }
    if (*length < stored.size()) { *length = stored.size(); return ESP_ERR_NVS_INVALID_LENGTH; }
    std::memcpy(value, stored.data(), stored.size()); *length = stored.size(); return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t, const char* key) {
    return values.erase(key) == 1 ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}

int main() {
    using zectrix::storage::StorageService;
    StorageService* service = nullptr;
    assert(StorageService::Create(&service) == ESP_OK);
    bool flag = false;
    assert(service->GetBool("flag", &flag) == ESP_ERR_INVALID_STATE);
    next_init_result = ESP_ERR_NVS_NO_FREE_PAGES;
    assert(service->Initialize() == ESP_OK && erase_count == 1);
    assert(service->Initialize() == ESP_OK && erase_count == 1);

    assert(service->SetBool("flag", true) == ESP_OK);
    assert(service->GetBool("flag", &flag) == ESP_OK && flag);
    assert(service->SetInt32("signed", -42) == ESP_OK);
    std::int32_t signed_value = 0;
    assert(service->GetInt32("signed", &signed_value) == ESP_OK && signed_value == -42);
    assert(service->SetUInt32("count", 42) == ESP_OK);
    std::uint32_t count = 0;
    assert(service->GetUInt32("count", &count) == ESP_OK && count == 42);
    assert(service->SetString("name", "note4") == ESP_OK);
    std::size_t string_length = 0;
    assert(service->GetString("name", nullptr, &string_length) == ESP_OK);
    char name[8] = {};
    assert(service->GetString("name", name, &string_length) == ESP_OK);
    assert(std::string(name) == "note4");
    const std::uint8_t blob[] = {1, 2, 3};
    assert(service->SetBlob("blob", blob, sizeof(blob)) == ESP_OK);
    std::uint8_t restored[3] = {};
    std::size_t blob_length = sizeof(restored);
    assert(service->GetBlob("blob", restored, &blob_length) == ESP_OK);
    assert(std::memcmp(blob, restored, sizeof(blob)) == 0);
    assert(service->Erase("flag") == ESP_OK);
    assert(service->GetBool("flag", &flag) == ESP_ERR_NOT_FOUND);
    assert(commit_count == 6);
    delete service;

    service = nullptr;
    assert(StorageService::Create(&service) == ESP_OK);
    assert(service->Initialize() == ESP_OK);
    count = 0;
    assert(service->GetUInt32("count", &count) == ESP_OK && count == 42);
    delete service;
}
