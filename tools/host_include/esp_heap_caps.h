#pragma once
#include <cstddef>
#include <cstdint>
constexpr uint32_t MALLOC_CAP_SPIRAM = 1U << 0;
constexpr uint32_t MALLOC_CAP_INTERNAL = 1U << 1;
size_t heap_caps_get_total_size(uint32_t capabilities);
size_t heap_caps_get_free_size(uint32_t capabilities);
size_t heap_caps_get_minimum_free_size(uint32_t capabilities);
size_t heap_caps_get_largest_free_block(uint32_t capabilities);
