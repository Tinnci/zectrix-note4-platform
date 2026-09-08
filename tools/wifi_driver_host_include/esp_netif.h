#pragma once

#include <cstdint>
#include "esp_err.h"

struct esp_netif_t {};
struct esp_netif_config_t {};
#define ESP_NETIF_DEFAULT_WIFI_STA() esp_netif_config_t{}
struct ip_event_got_ip_t {
    esp_netif_t* esp_netif;
    struct { struct { uint32_t addr; } ip; } ip_info;
};
esp_err_t esp_netif_init();
esp_netif_t* esp_netif_new(const esp_netif_config_t*);
void esp_netif_destroy(esp_netif_t*);
