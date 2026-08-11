#include "zectrix_system_service.h"

#include <cstdio>
#include <cstring>

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "zectrix_board.h"

namespace zectrix::system {
namespace {

template <std::size_t Size>
void CopyText(std::array<char, Size>* target, const char* source) {
    if (target == nullptr) return;
    std::snprintf(target->data(), target->size(), "%s",
                  source == nullptr ? "" : source);
}

ResetReason MapResetReason(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return ResetReason::PowerOn;
        case ESP_RST_SW: return ResetReason::Software;
        case ESP_RST_PANIC: return ResetReason::Panic;
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT: return ResetReason::Watchdog;
        case ESP_RST_DEEPSLEEP: return ResetReason::DeepSleep;
        case ESP_RST_BROWNOUT: return ResetReason::Brownout;
        case ESP_RST_EXT: return ResetReason::External;
        default: return ResetReason::Unknown;
    }
}

void FormatSha256(const uint8_t* sha,
                  std::array<char, 65>* destination) {
    if (sha == nullptr || destination == nullptr) return;
    for (std::size_t i = 0; i < 32; ++i) {
        std::snprintf(destination->data() + i * 2, 3, "%02x", sha[i]);
    }
}

}  // namespace

esp_err_t SystemService::Attach(ZectrixBoard& board,
                                SystemService** out_service) {
    if (out_service == nullptr) return ESP_ERR_INVALID_ARG;
    *out_service = new SystemService(board);
    return ESP_OK;
}

SystemService::~SystemService() = default;

esp_err_t SystemService::ReadSnapshot(SystemSnapshot* snapshot) const {
    if (snapshot == nullptr || board_ == nullptr) return ESP_ERR_INVALID_ARG;
    *snapshot = {};

    const esp_app_desc_t* app = esp_app_get_description();
    if (app != nullptr) {
        CopyText(&snapshot->firmware.project_name, app->project_name);
        CopyText(&snapshot->firmware.version, app->version);
        CopyText(&snapshot->firmware.idf_version, app->idf_ver);
        CopyText(&snapshot->firmware.build_date, app->date);
        CopyText(&snapshot->firmware.build_time, app->time);
        FormatSha256(app->app_elf_sha256,
                     &snapshot->firmware.app_elf_sha256);
    }

    esp_chip_info_t chip = {};
    esp_chip_info(&chip);
    CopyText(&snapshot->capabilities.chip_model, "ESP32-S3");
    snapshot->capabilities.chip_revision = chip.revision;
    snapshot->capabilities.core_count = chip.cores;
    snapshot->capabilities.wifi = (chip.features & CHIP_FEATURE_WIFI_BGN) != 0;
    snapshot->capabilities.bluetooth_le =
        (chip.features & CHIP_FEATURE_BLE) != 0;
    snapshot->capabilities.rtc = board_->HasRtc();
    snapshot->capabilities.nfc = board_->HasNfc();
    snapshot->diagnostics.psram_bytes = static_cast<uint32_t>(
        heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    snapshot->capabilities.psram = snapshot->diagnostics.psram_bytes != 0;
    snapshot->diagnostics.free_internal_heap_bytes = static_cast<uint32_t>(
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    snapshot->diagnostics.minimum_free_internal_heap_bytes =
        static_cast<uint32_t>(
            heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    snapshot->diagnostics.largest_internal_heap_block_bytes =
        static_cast<uint32_t>(
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    snapshot->reset_reason = MapResetReason(esp_reset_reason());

    esp_err_t err = esp_flash_get_size(
        nullptr, &snapshot->diagnostics.flash_bytes);
    if (err != ESP_OK) return err;
    return ReadWifiMac(&snapshot->wifi_mac);
}

esp_err_t SystemService::ReadWifiMac(std::array<uint8_t, 6>* mac) const {
    if (mac == nullptr) return ESP_ERR_INVALID_ARG;
    return esp_read_mac(mac->data(), ESP_MAC_WIFI_STA);
}

}  // namespace zectrix::system
