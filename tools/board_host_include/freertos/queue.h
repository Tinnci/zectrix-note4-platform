#pragma once

#include "freertos/FreeRTOS.h"
struct BoardHostQueue;
using QueueHandle_t = BoardHostQueue*;
QueueHandle_t xQueueCreate(UBaseType_t, UBaseType_t);
BaseType_t xQueueSend(QueueHandle_t, const void*, TickType_t);
BaseType_t xQueueReceive(QueueHandle_t, void*, TickType_t);
void vQueueDelete(QueueHandle_t);
