#pragma once

#include <cstdarg>

using vprintf_like_t = int (*)(const char*, va_list);
vprintf_like_t esp_log_set_vprintf(vprintf_like_t function);
enum esp_log_level_t {
    ESP_LOG_NONE,
    ESP_LOG_ERROR,
    ESP_LOG_WARN,
    ESP_LOG_INFO,
    ESP_LOG_DEBUG,
    ESP_LOG_VERBOSE
};
void esp_log_level_set(const char* tag, esp_log_level_t level);

template <typename... Arguments>
inline void HostEspLog(const char*, const char*, Arguments...) {}
#define ESP_LOGI(...) HostEspLog(__VA_ARGS__)
#define ESP_LOGD(...) HostEspLog(__VA_ARGS__)
#define ESP_LOGW(...) HostEspLog(__VA_ARGS__)
#define ESP_LOGE(...) HostEspLog(__VA_ARGS__)
