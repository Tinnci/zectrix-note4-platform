#pragma once

#include <cstddef>
#include "esp_err.h"

constexpr int ESP_TLS_ERR_SSL_WANT_READ = -10;
constexpr int ESP_TLS_ERR_SSL_WANT_WRITE = -11;
enum esp_tls_conn_state_t { ESP_TLS_INIT, ESP_TLS_CONNECTING };
struct esp_tls_t {};
struct esp_tls_cfg_t {
    bool non_block;
    int timeout_ms;
    const char* common_name;
    esp_err_t (*crt_bundle_attach)(void*);
};
esp_tls_t* esp_tls_init();
void esp_tls_conn_destroy(esp_tls_t*);
int esp_tls_conn_read(esp_tls_t*, void*, std::size_t);
int esp_tls_conn_write(esp_tls_t*, const void*, std::size_t);
esp_err_t esp_tls_get_conn_sockfd(esp_tls_t*, int*);
esp_err_t esp_tls_get_conn_state(esp_tls_t*, esp_tls_conn_state_t*);
int esp_tls_conn_new_async(const char*, int, int, const esp_tls_cfg_t*, esp_tls_t*);
