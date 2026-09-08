#pragma once

#include "freertos/FreeRTOS.h"
struct BoardHostSemaphore;
using SemaphoreHandle_t = BoardHostSemaphore*;
SemaphoreHandle_t xSemaphoreCreateBinary();
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex();
BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t);
BaseType_t xSemaphoreGive(SemaphoreHandle_t);
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t, TickType_t);
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t);
void vSemaphoreDelete(SemaphoreHandle_t);
