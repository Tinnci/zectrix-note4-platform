#pragma once

#include <cstdarg>

using vprintf_like_t = int (*)(const char*, va_list);
vprintf_like_t esp_log_set_vprintf(vprintf_like_t function);

template <typename... Arguments>
inline void HostEspLog(const char*, const char*, Arguments...) {}
#define ESP_LOGI(...) HostEspLog(__VA_ARGS__)
#define ESP_LOGD(...) HostEspLog(__VA_ARGS__)
#define ESP_LOGW(...) HostEspLog(__VA_ARGS__)
#define ESP_LOGE(...) HostEspLog(__VA_ARGS__)
