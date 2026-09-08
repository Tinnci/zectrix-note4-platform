#pragma once

#include <cstddef>

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

using esp_ota_handle_t = uint32_t;
constexpr std::size_t OTA_WITH_SEQUENTIAL_WRITES = 0xfffffffe;
constexpr esp_err_t ESP_ERR_OTA_PARTITION_CONFLICT = 0x1501;
constexpr esp_err_t ESP_ERR_OTA_VALIDATE_FAILED = 0x1503;
constexpr esp_err_t ESP_ERR_OTA_SMALL_SEC_VER = 0x1504;
constexpr esp_err_t ESP_ERR_OTA_ROLLBACK_INVALID_STATE = 0x1506;
esp_err_t esp_ota_begin(const esp_partition_t*, std::size_t, esp_ota_handle_t*);
esp_err_t esp_ota_write(esp_ota_handle_t, const void*, std::size_t);
esp_err_t esp_ota_end(esp_ota_handle_t);
esp_err_t esp_ota_abort(esp_ota_handle_t);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*);
