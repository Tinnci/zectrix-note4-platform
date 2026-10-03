#include "note4_edge_settings.h"
#include <algorithm>
#include <cstring>
#include "note4_storage_service.h"
namespace note4::connectivity {
namespace {
constexpr char kKey[] = "edge_settings";
constexpr std::size_t kBytes = 152;
uint32_t Read(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
void Write(uint8_t* p, uint32_t n) { for (unsigned i = 0; i < 4; ++i) p[i] = n >> (8 * i); }
int64_t AfterQuiet(const EdgeSettings& s, int64_t at, int32_t offset) {
    if (s.quiet_start_minute == s.quiet_end_minute) return at;
    const int64_t local = at + offset;
    const int64_t seconds = (local % 86400 + 86400) % 86400;
    const int start = s.quiet_start_minute * 60, end = s.quiet_end_minute * 60;
    const bool quiet = start < end ? seconds >= start && seconds < end : seconds >= start || seconds < end;
    if (!quiet) return at;
    return at + (end - seconds + 86400) % 86400;
}
}
bool ValidateEdgeSettings(const EdgeSettings& s) {
    const auto* host_end = static_cast<const char*>(std::memchr(s.host.data(), 0, s.host.size()));
    const auto* path_end = static_cast<const char*>(std::memchr(s.path.data(), 0, s.path.size()));
    if (!host_end || !path_end || s.interval_seconds < 300 || s.interval_seconds > 86400 ||
        s.budget_ms < 1000 || s.budget_ms > 60000 || s.minimum_battery_percent < 20 ||
        s.minimum_battery_percent > 100 || s.quiet_start_minute >= 1440 || s.quiet_end_minute >= 1440 || !s.source_generation) return false;
    if (host_end == s.host.data() || path_end == s.path.data()) {
        return !s.enabled && host_end == s.host.data() && path_end == s.path.data();
    }
    if (s.path[0] != '/' || s.host[0] == '.' || s.host[0] == '-' || host_end[-1] == '.' || host_end[-1] == '-') return false;
    for (const char* p = s.host.data(); p < host_end; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '.' || *p == '-')) return false;
    }
    for (const char* p = s.path.data(); p < path_end; ++p) {
        // Fixed path, no embedded credentials, fragment or query tokens.
        if (*p < 0x21 || *p > 0x7e || *p == '?' || *p == '#' || *p == '\\') return false;
    }
    return true;
}
esp_err_t StoredEdgeSettings::Load(EdgeSettings* settings) const {
    if (!settings) return ESP_ERR_INVALID_ARG;
    *settings = {};
    if (!storage_ || !storage_->IsInitialized()) return ESP_ERR_INVALID_STATE;
    std::array<uint8_t, kBytes> bytes{};
    std::size_t size = bytes.size();
    const auto result = storage_->GetBlob(kKey, bytes.data(), &size);
    if (result == ESP_ERR_NOT_FOUND) return ESP_OK;
    if (result != ESP_OK) return result;
    if (size != kBytes || bytes[0] != 1 || bytes[1] > 1 || bytes[2] > 1 || bytes[3] > 1) return ESP_ERR_INVALID_ARG;
    EdgeSettings decoded{};
    decoded.enabled = bytes[1]; decoded.show_page = bytes[2]; decoded.bthome_enabled = bytes[3];
    std::memcpy(decoded.host.data(), bytes.data() + 4, 64);
    std::memcpy(decoded.path.data(), bytes.data() + 68, 64);
    decoded.interval_seconds = Read(bytes.data() + 132); decoded.budget_ms = Read(bytes.data() + 136);
    decoded.minimum_battery_percent = bytes[140];
    if (bytes[141] || bytes[146] || bytes[147]) return ESP_ERR_INVALID_ARG;
    decoded.quiet_start_minute = bytes[142] | uint16_t(bytes[143]) << 8;
    decoded.quiet_end_minute = bytes[144] | uint16_t(bytes[145]) << 8;
    decoded.source_generation = Read(bytes.data() + 148);
    if (!ValidateEdgeSettings(decoded)) return ESP_ERR_INVALID_ARG;
    *settings = decoded;
    return ESP_OK;
}
esp_err_t StoredEdgeSettings::Save(const EdgeSettings& s) {
    if (!ValidateEdgeSettings(s)) return ESP_ERR_INVALID_ARG;
    if (!storage_ || !storage_->IsInitialized()) return ESP_ERR_INVALID_STATE;
    std::array<uint8_t, kBytes> bytes{}, old{};
    bytes[0] = 1; bytes[1] = s.enabled; bytes[2] = s.show_page; bytes[3] = s.bthome_enabled;
    std::memcpy(bytes.data() + 4, s.host.data(), std::strlen(s.host.data()));
    std::memcpy(bytes.data() + 68, s.path.data(), std::strlen(s.path.data()));
    Write(bytes.data() + 132, s.interval_seconds); Write(bytes.data() + 136, s.budget_ms);
    bytes[140] = s.minimum_battery_percent;
    bytes[142] = s.quiet_start_minute; bytes[143] = s.quiet_start_minute >> 8;
    bytes[144] = s.quiet_end_minute; bytes[145] = s.quiet_end_minute >> 8;
    std::size_t size = old.size();
    const auto read = storage_->GetBlob(kKey, old.data(), &size);
    uint32_t generation = s.source_generation;
    if (read == ESP_OK && size == kBytes && old[0] == 1) {
        generation = Read(old.data() + 148);
        if (std::memcmp(bytes.data() + 4, old.data() + 4, 128) != 0) {
            if (generation == UINT32_MAX) return ESP_ERR_INVALID_STATE;
            ++generation;
        }
    }
    Write(bytes.data() + 148, generation);
    if (read == ESP_OK && size == bytes.size() && old == bytes) return ESP_OK;
    if (read != ESP_OK && read != ESP_ERR_NOT_FOUND) return read;
    return storage_->SetBlob(kKey, bytes.data(), bytes.size());
}
bool EdgePowerAllowed(const EdgeSettings& s, bool external, bool valid, uint8_t battery) {
    return external || (valid && battery >= s.minimum_battery_percent);
}
esp_err_t StoredEdgeSettings::LoadToken(std::array<char, 65>* token) const {
    if (!token) return ESP_ERR_INVALID_ARG;
    token->fill(0);
    if (!storage_ || !storage_->IsInitialized()) return ESP_ERR_INVALID_STATE;
    std::size_t size = token->size();
    const auto read = storage_->GetString("edge_token", token->data(), &size);
    if (read == ESP_ERR_NOT_FOUND) return ESP_OK;
    if (read != ESP_OK) { token->fill(0); return read; }
    const auto* end = static_cast<const char*>(std::memchr(token->data(), 0, token->size()));
    if (!end || end - token->data() < 32) { token->fill(0); return ESP_ERR_INVALID_ARG; }
    for (const char* p = token->data(); p < end; ++p) if (!((*p >= '0' && *p <= '9') ||
        (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '-' || *p == '_')) {
        token->fill(0); return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}
esp_err_t StoredEdgeSettings::SaveToken(const char* token) {
    if (!storage_ || !storage_->IsInitialized()) return ESP_ERR_INVALID_STATE;
    if (!token) return ESP_ERR_INVALID_ARG;
    const std::size_t length = std::strlen(token);
    if (length == 0) {
        const auto erase = storage_->Erase("edge_token");
        return erase == ESP_ERR_NOT_FOUND ? ESP_OK : erase;
    }
    if (length < 32 || length > 64) return ESP_ERR_INVALID_ARG;
    for (const char* p = token; *p; ++p) if (!((*p >= '0' && *p <= '9') ||
        (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '-' || *p == '_')) return ESP_ERR_INVALID_ARG;
    return storage_->SetString("edge_token", token);
}
int64_t NextEdgeSync(const EdgeSettings& s, int64_t now, int64_t suggested, bool failed) {
    if (now < 1704067200) return 0;
    const int64_t normal = now + s.interval_seconds;
    if (failed || suggested <= now) return normal;
    return std::clamp(suggested, now + int64_t{300}, normal);
}
WakeDecision PlanWake(const EdgeSettings& s, int64_t now, int32_t offset,
                      int64_t next, uint64_t calendar, bool allowed) {
    WakeDecision result{calendar, false};
    if (!s.enabled || !allowed || now < 1704067200) return result;
    if (next <= 0 || next > now + 86400) next = now;
    else if (next > now + s.interval_seconds) next = now + s.interval_seconds;
    const int64_t at = AfterQuiet(s, std::max(now, next), offset);
    result.sync_due = at <= now;
    const uint64_t delay = result.sync_due ? 1000000 : uint64_t(at - now) * 1000000;
    if (!result.delay_us || delay < result.delay_us) result.delay_us = delay;
    return result;
}
}  // namespace note4::connectivity
