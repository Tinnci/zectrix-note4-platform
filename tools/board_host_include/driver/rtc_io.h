#pragma once

#include "driver/gpio.h"

enum rtc_gpio_mode_t { RTC_GPIO_MODE_INPUT_ONLY };
esp_err_t rtc_gpio_init(gpio_num_t);
esp_err_t rtc_gpio_deinit(gpio_num_t);
esp_err_t rtc_gpio_hold_dis(gpio_num_t);
esp_err_t rtc_gpio_set_direction(gpio_num_t, rtc_gpio_mode_t);
esp_err_t rtc_gpio_pullup_en(gpio_num_t);
esp_err_t rtc_gpio_pulldown_dis(gpio_num_t);
