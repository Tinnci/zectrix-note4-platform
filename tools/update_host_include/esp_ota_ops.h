#pragma once

#include "esp_err.h"
#include "esp_partition.h"

enum esp_ota_img_states_t : uint32_t {
    ESP_OTA_IMG_NEW, ESP_OTA_IMG_PENDING_VERIFY, ESP_OTA_IMG_VALID,
    ESP_OTA_IMG_INVALID, ESP_OTA_IMG_ABORTED, ESP_OTA_IMG_UNDEFINED = 0xffffffff,
};
const esp_partition_t* esp_ota_get_running_partition();
const esp_partition_t* esp_ota_get_boot_partition();
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*);
esp_err_t esp_ota_get_state_partition(const esp_partition_t*, esp_ota_img_states_t*);
bool esp_ota_check_rollback_is_possible();
esp_err_t esp_ota_mark_app_valid_cancel_rollback();
