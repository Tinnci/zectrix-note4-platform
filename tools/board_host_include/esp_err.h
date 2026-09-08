#pragma once

#include <cassert>
#include "../host_include/esp_err.h"

constexpr esp_err_t ESP_ERR_TIMEOUT = 0x109;
const char* esp_err_to_name(esp_err_t);
#define ESP_ERROR_CHECK(value) assert((value) == ESP_OK)
#define ESP_ERROR_CHECK_WITHOUT_ABORT(value) (void)(value)
