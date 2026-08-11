#pragma once

#include "freertos/FreeRTOS.h"

void vTaskDelay(TickType_t ticks);

#define pdMS_TO_TICKS(ms) static_cast<TickType_t>(ms)
