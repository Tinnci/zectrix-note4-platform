#pragma once

#include <cassert>
#include <cstdint>
using TickType_t = uint32_t;
using BaseType_t = int;
using UBaseType_t = unsigned;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;
constexpr BaseType_t pdTRUE = 1, pdFALSE = 0, pdPASS = 1;
#define pdMS_TO_TICKS(ms) static_cast<TickType_t>(ms)
#define configASSERT(value) assert(value)
#define portYIELD_FROM_ISR() ((void)0)
