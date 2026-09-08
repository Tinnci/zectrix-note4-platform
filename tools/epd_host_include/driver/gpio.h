#pragma once

#include <cstdint>

#include "esp_err.h"

using gpio_num_t = int;
enum { GPIO_NUM_6 = 6, GPIO_NUM_8 = 8, GPIO_NUM_9 = 9, GPIO_NUM_10 = 10,
       GPIO_NUM_11 = 11, GPIO_NUM_12 = 12, GPIO_NUM_13 = 13 };
enum { GPIO_MODE_INPUT, GPIO_MODE_OUTPUT, GPIO_PULLUP_ENABLE };
struct gpio_config_t { uint64_t pin_bit_mask; int mode; int pull_up_en; };
constexpr bool GPIO_IS_VALID_GPIO(gpio_num_t pin) { return pin >= 0 && pin < 49; }
constexpr bool GPIO_IS_VALID_OUTPUT_GPIO(gpio_num_t pin) { return GPIO_IS_VALID_GPIO(pin); }
esp_err_t gpio_config(const gpio_config_t*);
esp_err_t gpio_set_level(gpio_num_t, int);
int gpio_get_level(gpio_num_t);
esp_err_t gpio_hold_dis(gpio_num_t);
esp_err_t gpio_hold_en(gpio_num_t);
