#pragma once

#include "freertos/FreeRTOS.h"

struct CliHostTask;
using TaskHandle_t = CliHostTask*;
using TaskFunction_t = void (*)(void*);
constexpr BaseType_t pdPASS = pdTRUE;
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char*, uint32_t, void*,
                                  unsigned, TaskHandle_t*, BaseType_t);
BaseType_t xPortGetCoreID();
void xTaskNotifyGive(TaskHandle_t);
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t);
void vTaskDelay(TickType_t);
void vTaskDelete(TaskHandle_t);
