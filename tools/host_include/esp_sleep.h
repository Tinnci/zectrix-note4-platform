#pragma once

#include <cstdint>
#include "esp_err.h"

enum esp_sleep_wakeup_cause_t {
    ESP_SLEEP_WAKEUP_UNDEFINED,
    ESP_SLEEP_WAKEUP_ALL,
    ESP_SLEEP_WAKEUP_EXT0,
    ESP_SLEEP_WAKEUP_EXT1,
    ESP_SLEEP_WAKEUP_TIMER,
    ESP_SLEEP_WAKEUP_TOUCHPAD,
    ESP_SLEEP_WAKEUP_ULP,
    ESP_SLEEP_WAKEUP_GPIO,
    ESP_SLEEP_WAKEUP_UART,
    ESP_SLEEP_WAKEUP_WIFI,
    ESP_SLEEP_WAKEUP_COCPU,
    ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG,
    ESP_SLEEP_WAKEUP_BT,
};

esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause();
enum esp_sleep_ext1_wakeup_mode_t { ESP_EXT1_WAKEUP_ANY_LOW, ESP_EXT1_WAKEUP_ANY_HIGH };
esp_err_t esp_sleep_enable_ext1_wakeup_io(uint64_t mask, esp_sleep_ext1_wakeup_mode_t mode);
[[noreturn]] void esp_deep_sleep_start();
