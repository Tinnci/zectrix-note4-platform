#pragma once

#include <cstdint>

#include "esp_err.h"

constexpr esp_err_t ESP_ERR_IMAGE_FLASH_FAIL = 0x2001;
constexpr esp_err_t ESP_ERR_IMAGE_INVALID = 0x2002;
struct esp_partition_pos_t { uint32_t offset; uint32_t size; };
struct esp_image_metadata_t { uint32_t image_len; };
esp_err_t esp_image_get_metadata(const esp_partition_pos_t*, esp_image_metadata_t*);
