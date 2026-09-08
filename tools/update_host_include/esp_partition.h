#pragma once

#include <cstdint>

enum esp_partition_type_t { ESP_PARTITION_TYPE_APP = 0, ESP_PARTITION_TYPE_DATA = 1, ESP_PARTITION_TYPE_ANY = 0xff };
enum esp_partition_subtype_t {
    ESP_PARTITION_SUBTYPE_APP_FACTORY = 0, ESP_PARTITION_SUBTYPE_APP_OTA_MIN = 0x10,
    ESP_PARTITION_SUBTYPE_APP_OTA_0 = 0x10, ESP_PARTITION_SUBTYPE_APP_OTA_1 = 0x11,
    ESP_PARTITION_SUBTYPE_APP_OTA_MAX = 0x20, ESP_PARTITION_SUBTYPE_DATA_OTA = 0,
    ESP_PARTITION_SUBTYPE_ANY = 0xff,
};
struct esp_partition_t {
    esp_partition_type_t type = ESP_PARTITION_TYPE_APP;
    esp_partition_subtype_t subtype = ESP_PARTITION_SUBTYPE_APP_FACTORY;
    uint32_t address = 0;
    uint32_t size = 0;
    bool readonly = false;
};
struct esp_partition_iterator_opaque_;
using esp_partition_iterator_t = esp_partition_iterator_opaque_*;
esp_partition_iterator_t esp_partition_find(esp_partition_type_t, esp_partition_subtype_t, const char*);
const esp_partition_t* esp_partition_get(esp_partition_iterator_t);
esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t);
void esp_partition_iterator_release(esp_partition_iterator_t);
