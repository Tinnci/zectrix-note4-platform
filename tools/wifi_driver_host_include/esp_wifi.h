#pragma once

#include <cstdint>
#include "esp_err.h"

constexpr esp_err_t ESP_ERR_WIFI_NOT_STARTED = 0x3002;
enum { WIFI_REASON_AUTH_FAIL, WIFI_REASON_AUTH_EXPIRE, WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT,
       WIFI_REASON_HANDSHAKE_TIMEOUT, WIFI_REASON_802_1X_AUTH_FAILED };
enum { WIFI_STORAGE_RAM, WIFI_MODE_STA, WIFI_ALL_CHANNEL_SCAN, WIFI_AUTH_OPEN,
       WIFI_AUTH_WPA2_PSK, WPA3_SAE_PWE_BOTH, WIFI_IF_STA, WIFI_SCAN_TYPE_ACTIVE };
struct wifi_event_sta_disconnected_t { uint8_t reason; };
struct wifi_init_config_t {};
#define WIFI_INIT_CONFIG_DEFAULT() wifi_init_config_t{}
struct wifi_config_t {
    struct {
        uint8_t ssid[32], password[64];
        int scan_method;
        struct { int authmode; } threshold;
        struct { bool capable; } pmf_cfg;
        int sae_pwe_h2e;
    } sta;
};
struct wifi_scan_config_t { bool show_hidden; int scan_type; uint8_t* ssid; };
struct wifi_ap_record_t { uint8_t ssid[33]; int8_t rssi; };
esp_err_t esp_wifi_init(const wifi_init_config_t*);
esp_err_t esp_wifi_deinit();
esp_err_t esp_wifi_set_storage(int);
esp_err_t esp_wifi_set_mode(int);
esp_err_t esp_wifi_set_config(int, const wifi_config_t*);
esp_err_t esp_wifi_start();
esp_err_t esp_wifi_stop();
esp_err_t esp_wifi_connect();
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t*, bool);
esp_err_t esp_wifi_scan_get_ap_num(uint16_t*);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t*, wifi_ap_record_t*);
esp_err_t esp_wifi_clear_ap_list();
