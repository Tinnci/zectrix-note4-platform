#pragma once

#include <cstdint>
#include "esp_err.h"
using gpio_num_t = int;
enum {
    GPIO_NUM_NC = -1, GPIO_NUM_0 = 0, GPIO_NUM_1 = 1, GPIO_NUM_2 = 2,
    GPIO_NUM_3 = 3, GPIO_NUM_5 = 5, GPIO_NUM_7 = 7, GPIO_NUM_14 = 14,
    GPIO_NUM_15 = 15, GPIO_NUM_16 = 16, GPIO_NUM_17 = 17, GPIO_NUM_18 = 18,
    GPIO_NUM_21 = 21, GPIO_NUM_38 = 38, GPIO_NUM_39 = 39, GPIO_NUM_42 = 42,
    GPIO_NUM_45 = 45, GPIO_NUM_46 = 46, GPIO_NUM_47 = 47, GPIO_NUM_48 = 48,
};
enum { GPIO_MODE_DISABLE, GPIO_MODE_INPUT, GPIO_MODE_OUTPUT };
enum { GPIO_PULLUP_DISABLE, GPIO_PULLUP_ENABLE };
enum { GPIO_PULLDOWN_DISABLE, GPIO_PULLDOWN_ENABLE };
enum { GPIO_INTR_DISABLE, GPIO_INTR_ANYEDGE };
struct gpio_config_t {
    uint64_t pin_bit_mask;
    int mode, pull_up_en, pull_down_en, intr_type;
};
esp_err_t gpio_config(const gpio_config_t*);
esp_err_t gpio_set_level(gpio_num_t, uint32_t);
int gpio_get_level(gpio_num_t);
esp_err_t gpio_hold_dis(gpio_num_t);
esp_err_t gpio_hold_en(gpio_num_t);
void gpio_deep_sleep_hold_en();
void gpio_deep_sleep_hold_dis();
esp_err_t gpio_install_isr_service(int);
esp_err_t gpio_isr_handler_add(gpio_num_t, void (*)(void*), void*);
esp_err_t gpio_isr_handler_remove(gpio_num_t);
