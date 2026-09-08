#pragma once

#include <cstdint>
#include "esp_err.h"

using esp_event_base_t = const char*;
using esp_event_handler_t = void (*)(void*, esp_event_base_t, int32_t, void*);
struct WifiHostHandler;
using esp_event_handler_instance_t = WifiHostHandler*;
inline constexpr char WIFI_EVENT[] = "wifi";
inline constexpr char IP_EVENT[] = "ip";
constexpr int32_t ESP_EVENT_ANY_ID = -1;
enum { WIFI_EVENT_STA_START, WIFI_EVENT_STA_CONNECTED, WIFI_EVENT_STA_DISCONNECTED,
       WIFI_EVENT_SCAN_DONE, IP_EVENT_STA_GOT_IP, IP_EVENT_STA_LOST_IP };
esp_err_t esp_event_loop_create_default();
esp_err_t esp_event_handler_instance_register(esp_event_base_t, int32_t,
                                              esp_event_handler_t, void*, esp_event_handler_instance_t*);
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t, int32_t,
                                                esp_event_handler_instance_t);
