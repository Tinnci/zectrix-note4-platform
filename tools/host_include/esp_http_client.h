#pragma once

#include "esp_transport.h"

struct esp_http_client;
using esp_http_client_handle_t = esp_http_client*;
enum esp_http_client_method_t { HTTP_METHOD_GET };
enum esp_http_client_transport_t { HTTP_TRANSPORT_OVER_SSL };

struct esp_http_client_config_t {
    const char* url = nullptr;
    esp_http_client_method_t method = HTTP_METHOD_GET;
    esp_http_client_transport_t transport_type = HTTP_TRANSPORT_OVER_SSL;
    esp_transport_handle_t transport = nullptr;
    bool is_async = false;
    int timeout_ms = 0;
    bool disable_auto_redirect = false;
    int max_authorization_retries = 0;
    int buffer_size = 0;
    int buffer_size_tx = 0;
    const char* user_agent = nullptr;
};

constexpr esp_err_t ESP_ERR_HTTP_EAGAIN = 0x7007;
constexpr esp_err_t ESP_ERR_HTTP_CONNECTION_CLOSED = 0x7008;
constexpr esp_err_t ESP_ERR_HTTP_INCOMPLETE_DATA = 0x700c;

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* config);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client,
                                    const char* key, const char* value);
esp_err_t esp_http_client_perform(esp_http_client_handle_t client);
int esp_http_client_get_status_code(esp_http_client_handle_t client);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);
