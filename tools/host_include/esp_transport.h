#pragma once

#include "esp_err.h"

struct esp_transport;
using esp_transport_handle_t = esp_transport*;
using transport_connect = int (*)(esp_transport_handle_t, const char*, int, int);
using transport_read = int (*)(esp_transport_handle_t, char*, int, int);
using transport_write = int (*)(esp_transport_handle_t, const char*, int, int);
using transport_close = int (*)(esp_transport_handle_t);
using transport_poll = int (*)(esp_transport_handle_t, int);
using transport_destroy = int (*)(esp_transport_handle_t);

constexpr int ERR_TCP_TRANSPORT_CONNECTION_TIMEOUT = 0;
constexpr int ERR_TCP_TRANSPORT_CONNECTION_CLOSED_BY_FIN = -1;
constexpr int ERR_TCP_TRANSPORT_CONNECTION_FAILED = -2;

esp_transport_handle_t esp_transport_init();
esp_err_t esp_transport_destroy(esp_transport_handle_t transport);
void* esp_transport_get_context_data(esp_transport_handle_t transport);
esp_err_t esp_transport_set_context_data(esp_transport_handle_t transport, void* context);
esp_err_t esp_transport_set_default_port(esp_transport_handle_t transport, int port);
esp_err_t esp_transport_set_async_connect_func(esp_transport_handle_t transport,
                                              transport_connect connect);
esp_err_t esp_transport_set_func(esp_transport_handle_t transport,
    transport_connect connect, transport_read read, transport_write write,
    transport_close close, transport_poll poll_read, transport_poll poll_write,
    transport_destroy destroy);
