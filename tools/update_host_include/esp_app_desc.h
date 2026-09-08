#pragma once

#include <cstdint>

constexpr uint32_t ESP_APP_DESC_MAGIC_WORD = 0xabcd5432;
struct esp_app_desc_t {
    uint32_t magic_word;
    uint32_t secure_version;
    uint8_t remaining[248];
};
