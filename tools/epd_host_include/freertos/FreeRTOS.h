#pragma once

#include <cstdint>

using TickType_t = uint32_t;
using BaseType_t = int;
constexpr BaseType_t pdTRUE = 1;
constexpr BaseType_t pdFALSE = 0;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;
constexpr TickType_t pdMS_TO_TICKS(uint32_t ms) { return ms; }
