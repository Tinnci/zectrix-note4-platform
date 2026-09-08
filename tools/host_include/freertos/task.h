#pragma once

#include "freertos/FreeRTOS.h"

#define configUSE_TRACE_FACILITY 1
using TaskHandle_t = void*;
using UBaseType_t = unsigned int;
enum eTaskState { eRunning, eReady, eBlocked, eSuspended, eDeleted, eInvalid };
struct TaskStatus_t {
    TaskHandle_t xHandle = nullptr;
    const char* pcTaskName = nullptr;
    UBaseType_t xTaskNumber = 0;
    eTaskState eCurrentState = eInvalid;
    UBaseType_t uxCurrentPriority = 0;
    uint32_t usStackHighWaterMark = 0;
};

inline thread_local TaskHandle_t host_current_task = nullptr;
inline TickType_t host_ticks = 0;
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return host_current_task; }
inline TickType_t xTaskGetTickCount() { return host_ticks; }
UBaseType_t uxTaskGetNumberOfTasks();
UBaseType_t uxTaskGetSystemState(TaskStatus_t* tasks, UBaseType_t capacity,
                                uint32_t* runtime);

void vTaskDelay(TickType_t ticks);

#define pdMS_TO_TICKS(ms) static_cast<TickType_t>(ms)
