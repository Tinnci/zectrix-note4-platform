#pragma once

#include <cstddef>
#include <cstdint>

constexpr uint32_t MALLOC_CAP_DMA = 1U << 0;
constexpr uint32_t MALLOC_CAP_INTERNAL = 1U << 1;
constexpr uint32_t MALLOC_CAP_8BIT = 1U << 2;
void* heap_caps_malloc(std::size_t, uint32_t);
void heap_caps_free(void*);
