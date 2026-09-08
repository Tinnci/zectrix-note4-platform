#pragma once

#include "freertos/FreeRTOS.h"
struct BoardHostTask;
using TaskHandle_t = BoardHostTask*;
using TaskFunction_t = void (*)(void*);
enum eNotifyAction { eSetBits };
BaseType_t xTaskCreate(TaskFunction_t, const char*, uint32_t, void*, UBaseType_t, TaskHandle_t*);
BaseType_t xTaskNotify(TaskHandle_t, uint32_t, eNotifyAction);
BaseType_t xTaskNotifyFromISR(TaskHandle_t, uint32_t, eNotifyAction, BaseType_t*);
BaseType_t xTaskNotifyWait(uint32_t, uint32_t, uint32_t*, TickType_t);
TickType_t xTaskGetTickCount();
void vTaskDelay(TickType_t);
void vTaskDelete(TaskHandle_t);
