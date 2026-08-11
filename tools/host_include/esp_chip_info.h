#pragma once
#include <cstdint>
constexpr uint32_t CHIP_FEATURE_WIFI_BGN = 1U << 0;
constexpr uint32_t CHIP_FEATURE_BLE = 1U << 1;
struct esp_chip_info_t {
    uint32_t features = 0;
    uint8_t cores = 0;
    uint16_t revision = 0;
};
void esp_chip_info(esp_chip_info_t* info);
