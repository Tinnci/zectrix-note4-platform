#pragma once

#include "freertos/FreeRTOS.h"

struct CliHostSemaphore;
using SemaphoreHandle_t = CliHostSemaphore*;
SemaphoreHandle_t xSemaphoreCreateBinary();
BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t);
BaseType_t xSemaphoreGive(SemaphoreHandle_t);
void vSemaphoreDelete(SemaphoreHandle_t);
