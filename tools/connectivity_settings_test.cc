#include <cassert>
#include <cstdint>
#include <cstring>
#include "note4_connectivity_settings.h"
#include "note4_storage_service.h"
#include "note4_wifi_credentials.h"
#include "note4_edge_settings.h"

namespace {
bool ready = true;
esp_err_t read_result = ESP_ERR_NOT_FOUND, write_result = ESP_OK;
uint32_t value = 0;
unsigned writes = 0;
note4::connectivity::WifiCredentials station{};
std::size_t station_size = sizeof(station);
esp_err_t blob_result = ESP_ERR_NOT_FOUND;
std::array<uint8_t, 152> edge_blob{};
esp_err_t edge_result = ESP_ERR_NOT_FOUND;
unsigned edge_writes = 0;
std::array<char, 65> token_store{};
bool token_present = false;
}
namespace note4::storage {
struct StorageService::Impl {};
esp_err_t StorageService::Create(StorageService** output) {
    *output = new StorageService(nullptr);
    return ESP_OK;
}
StorageService::~StorageService() = default;
bool StorageService::IsInitialized() const { return ready; }
esp_err_t StorageService::GetString(const char* key, char* output, std::size_t* size) const {
    assert(std::strcmp(key, "edge_token") == 0);
    if (!token_present) return ESP_ERR_NOT_FOUND;
    assert(*size >= token_store.size());
    std::memcpy(output, token_store.data(), token_store.size()); return ESP_OK;
}
esp_err_t StorageService::SetString(const char* key, const char* input) {
    assert(std::strcmp(key, "edge_token") == 0);
    if (write_result != ESP_OK) return write_result;
    std::strcpy(token_store.data(), input); token_present = true; return ESP_OK;
}
esp_err_t StorageService::GetUInt32(const char* key, uint32_t* output) const {
    assert(std::strcmp(key, "conn_policy") == 0);
    if (read_result == ESP_OK) *output = value;
    return read_result;
}
esp_err_t StorageService::SetUInt32(const char* key, uint32_t input) {
    assert(std::strcmp(key, "conn_policy") == 0);
    ++writes;
    if (write_result == ESP_OK) { value = input; read_result = ESP_OK; }
    return write_result;
}
esp_err_t StorageService::GetBlob(const char* key, void* output, std::size_t* size) const {
    if (std::strcmp(key, "edge_settings") == 0) {
        if (edge_result != ESP_OK) return edge_result;
        assert(*size >= edge_blob.size());
        std::memcpy(output, edge_blob.data(), edge_blob.size()); *size = edge_blob.size();
        return ESP_OK;
    }
    assert(std::strcmp(key, "wifi_station") == 0);
    if (blob_result != ESP_OK) return blob_result;
    assert(*size >= station_size);
    std::memcpy(output, &station, station_size);
    *size = station_size;
    return ESP_OK;
}
esp_err_t StorageService::SetBlob(const char* key, const void* input, std::size_t size) {
    if (std::strcmp(key, "edge_settings") == 0) {
        assert(size == edge_blob.size());
        if (write_result != ESP_OK) return write_result;
        std::memcpy(edge_blob.data(), input, size); edge_result = ESP_OK; ++edge_writes;
        return ESP_OK;
    }
    assert(std::strcmp(key, "wifi_station") == 0 && size == sizeof(station));
    if (write_result != ESP_OK) return write_result;
    std::memcpy(&station, input, size);
    station_size = size;
    blob_result = ESP_OK;
    return ESP_OK;
}
esp_err_t StorageService::Erase(const char* key) {
    if (std::strcmp(key, "edge_token") == 0) {
        if (write_result != ESP_OK) return write_result;
        token_present = false; token_store = {}; return ESP_OK;
    }
    assert(std::strcmp(key, "wifi_station") == 0);
    if (write_result != ESP_OK) return write_result;
    if (blob_result == ESP_ERR_NOT_FOUND) return ESP_ERR_NOT_FOUND;
    blob_result = ESP_ERR_NOT_FOUND;
    station = {};
    return ESP_OK;
}
}
int main() {
    using namespace note4;
    using Policy = companion::UserConnectivityPolicy;
    storage::StorageService* storage = nullptr;
    assert(storage::StorageService::Create(&storage) == ESP_OK);
    connectivity::StoredConnectivitySettings settings(storage);
    Policy policy = Policy::kOffline;
    assert(settings.LoadPolicy(nullptr) == ESP_ERR_INVALID_ARG);
    assert(settings.LoadPolicy(&policy) == ESP_OK && policy == Policy::kAutomatic);
    assert(writes == 0);
    for (uint32_t i = 0; i <= static_cast<uint32_t>(Policy::kOffline); ++i) {
        assert(settings.SavePolicy(static_cast<Policy>(i)) == ESP_OK);
        assert(settings.LoadPolicy(&policy) == ESP_OK && static_cast<uint32_t>(policy) == i);
        const unsigned before = writes;
        assert(settings.SavePolicy(policy) == ESP_OK && writes == before);
    }
    value = 99;
    assert(settings.LoadPolicy(&policy) == ESP_ERR_INVALID_ARG && policy == Policy::kOffline);
    const unsigned before = writes;
    assert(settings.SavePolicy(static_cast<Policy>(99)) == ESP_ERR_INVALID_ARG && writes == before);
    // An explicit valid local change can repair an invalid numeric preference.
    assert(settings.SavePolicy(Policy::kPhoneOnly) == ESP_OK);
    read_result = ESP_FAIL;
    assert(settings.LoadPolicy(&policy) == ESP_FAIL && policy == Policy::kOffline);
    const unsigned failed_read_writes = writes;
    assert(settings.SavePolicy(Policy::kAutomatic) == ESP_FAIL && writes == failed_read_writes);
    read_result = ESP_OK;
    write_result = ESP_FAIL;
    assert(settings.SavePolicy(Policy::kWifiOnly) == ESP_FAIL);
    assert(settings.LoadPolicy(&policy) == ESP_OK && policy == Policy::kPhoneOnly);
    ready = false;
    assert(settings.LoadPolicy(&policy) == ESP_ERR_INVALID_STATE && policy == Policy::kOffline);
    assert(settings.SavePolicy(Policy::kAutomatic) == ESP_ERR_INVALID_STATE);
    connectivity::StoredConnectivitySettings absent(nullptr);
    assert(absent.LoadPolicy(&policy) == ESP_ERR_INVALID_STATE && policy == Policy::kOffline);
    ready = true;
    write_result = ESP_OK;
    connectivity::StoredWifiCredentials wifi(storage);
    connectivity::WifiCredentials credentials{}, loaded{};
    std::strcpy(credentials.ssid.data(), "test-network");
    std::strcpy(credentials.passphrase.data(), "test-password");
    assert(wifi.Save(credentials) == ESP_OK && wifi.Available());
    assert(wifi.Load(&loaded) == connectivity::WifiCredentialResult::kAvailable);
    assert(loaded.ssid == credentials.ssid && loaded.passphrase == credentials.passphrase);
    // Failed saves and erases preserve the previously committed credentials.
    write_result = ESP_FAIL;
    std::strcpy(credentials.ssid.data(), "replacement");
    assert(wifi.Save(credentials) == ESP_FAIL);
    assert(wifi.Erase() == ESP_FAIL && wifi.Available());
    assert(wifi.Load(&loaded) == connectivity::WifiCredentialResult::kAvailable);
    assert(std::strcmp(loaded.ssid.data(), "test-network") == 0);
    station_size = 1;
    assert(wifi.Load(&loaded) == connectivity::WifiCredentialResult::kInvalid);
    for (char byte : loaded.passphrase) assert(byte == 0);
    station_size = sizeof(station);
    station.ssid.fill('x');
    assert(wifi.Load(&loaded) == connectivity::WifiCredentialResult::kInvalid);
    for (char byte : loaded.ssid) assert(byte == 0);
    write_result = ESP_OK;
    assert(wifi.Erase() == ESP_OK && !wifi.Available());
    assert(wifi.Erase() == ESP_ERR_NOT_FOUND);
    assert(wifi.Load(&loaded) == connectivity::WifiCredentialResult::kUnavailable);
    connectivity::StoredEdgeSettings edge(storage);
    std::array<char, 65> token;
    assert(edge.LoadToken(&token) == ESP_OK && token[0] == 0);
    assert(edge.SaveToken("short") == ESP_ERR_INVALID_ARG);
    assert(edge.SaveToken("0123456789abcdef0123456789abcdef") == ESP_OK);
    assert(edge.LoadToken(&token) == ESP_OK && std::strlen(token.data()) == 32);
    assert(edge.SaveToken("") == ESP_OK);
    assert(edge.LoadToken(&token) == ESP_OK && token[0] == 0);
    connectivity::EdgeSettings s, restored;
    assert(edge.Load(&s) == ESP_OK && !s.enabled && !s.show_page && !s.bthome_enabled);
    assert(connectivity::ValidateEdgeSettings(s));
    s.enabled = true;
    assert(!connectivity::ValidateEdgeSettings(s));
    std::strcpy(s.host.data(), "display.example.com"); std::strcpy(s.path.data(), "/note4/page");
    assert(edge.Save(s) == ESP_OK);
    assert(edge.Load(&restored) == ESP_OK && restored.enabled && restored.source_generation == 1);
    assert(edge.Save(restored) == ESP_OK && edge_writes == 1);
    restored.bthome_enabled = true;
    assert(edge.Save(restored) == ESP_OK);
    assert(edge.Load(&restored) == ESP_OK && restored.bthome_enabled);
    edge_blob[3] = 2;
    assert(edge.Load(&restored) == ESP_ERR_INVALID_ARG && !restored.bthome_enabled);
    edge_blob[3] = 1;
    assert(edge.Load(&restored) == ESP_OK);
    std::strcpy(restored.path.data(), "/other");
    assert(edge.Save(restored) == ESP_OK);
    assert(edge.Load(&restored) == ESP_OK && restored.source_generation == 2);
    write_result = ESP_FAIL;
    restored.enabled = false;
    assert(edge.Save(restored) == ESP_FAIL);
    assert(edge.Load(&restored) == ESP_OK && restored.enabled);
    write_result = ESP_OK;
    edge_blob[0] = 99;
    assert(edge.Load(&restored) == ESP_ERR_INVALID_ARG && !restored.enabled);
    edge_result = ESP_FAIL;
    assert(edge.Save(s) == ESP_FAIL);
    assert(!connectivity::EdgePowerAllowed(s, false, false, 100));
    assert(connectivity::EdgePowerAllowed(s, true, false, 0));
    assert(!connectivity::EdgePowerAllowed(s, false, true, 19));
    assert(connectivity::EdgePowerAllowed(s, false, true, 20));
    constexpr int64_t midnight = 1704067200;
    auto decision = connectivity::PlanWake(s, midnight, 0, 0, 60000000, true);
    assert(decision.sync_due && decision.delay_us == 1000000);
    assert(!connectivity::PlanWake(s, midnight, 0, 0, 60000000, false).sync_due);
    assert(connectivity::PlanWake(s, 0, 0, 0, 60000000, true).delay_us == 60000000);
    s.quiet_start_minute = 22 * 60; s.quiet_end_minute = 7 * 60;
    decision = connectivity::PlanWake(s, midnight + 23 * 3600, 0, midnight, 0, true);
    assert(!decision.sync_due && decision.delay_us == 8ULL * 3600 * 1000000);
    decision = connectivity::PlanWake(s, midnight, 8 * 3600, midnight, 0, true);
    assert(decision.sync_due);  // UTC+8 is already outside quiet hours.
    assert(connectivity::NextEdgeSync(s, midnight, midnight + 1, false) == midnight + 300);
    assert(connectivity::NextEdgeSync(s, midnight, midnight + 86400, false) == midnight + 21600);
    assert(connectivity::NextEdgeSync(s, midnight, midnight + 300, true) == midnight + 21600);
    s.minimum_battery_percent = 19; assert(!connectivity::ValidateEdgeSettings(s)); s.minimum_battery_percent = 20;
    std::strcpy(s.host.data(), "user@host"); assert(!connectivity::ValidateEdgeSettings(s));
    std::strcpy(s.host.data(), "example.com"); std::strcpy(s.path.data(), "/page?secret=bad"); assert(!connectivity::ValidateEdgeSettings(s));
    delete storage;
}
