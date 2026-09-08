#pragma once

#include "esp_app_format.h"
#include "esp_err.h"

enum esp_image_type { ESP_IMAGE_BOOTLOADER, ESP_IMAGE_APPLICATION };
esp_err_t bootloader_common_check_chip_validity(const esp_image_header_t*, esp_image_type);
