#pragma once

#include <cstdarg>

using vprintf_like_t = int (*)(const char*, va_list);
vprintf_like_t esp_log_set_vprintf(vprintf_like_t function);
