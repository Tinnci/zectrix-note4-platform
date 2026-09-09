#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"

using httpd_handle_t = void*;
enum httpd_method_t { HTTP_GET, HTTP_PUT, HTTP_DELETE, HTTP_POST };
struct httpd_req_t {
    const char* uri;
    httpd_method_t method;
    std::size_t content_len;
    void* user_ctx;
    int socket;
    void* test;
};
struct httpd_uri_t {
    const char* uri;
    httpd_method_t method;
    esp_err_t (*handler)(httpd_req_t*);
    void* user_ctx;
};
struct httpd_config_t {
    std::size_t stack_size = 4096;
    unsigned max_open_sockets = 4, max_uri_handlers = 4;
    uint16_t recv_wait_timeout = 5, send_wait_timeout = 5;
    bool lru_purge_enable = false;
    bool (*uri_match_fn)(const char*, const char*, std::size_t) = nullptr;
    void* global_user_ctx = nullptr;
    void (*global_user_ctx_free_fn)(void*) = nullptr;
    esp_err_t (*open_fn)(httpd_handle_t, int) = nullptr;
    void (*close_fn)(httpd_handle_t, int) = nullptr;
};
#define HTTPD_DEFAULT_CONFIG() httpd_config_t{}
bool httpd_uri_match_wildcard(const char*, const char*, std::size_t);
esp_err_t httpd_start(httpd_handle_t*, const httpd_config_t*);
esp_err_t httpd_stop(httpd_handle_t);
esp_err_t httpd_register_uri_handler(httpd_handle_t, const httpd_uri_t*);
void* httpd_get_global_user_ctx(httpd_handle_t);
int httpd_req_to_sockfd(httpd_req_t*);
std::size_t httpd_req_get_hdr_value_len(httpd_req_t*, const char*);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t*, const char*, char*, std::size_t);
int httpd_req_recv(httpd_req_t*, char*, std::size_t);
esp_err_t httpd_resp_set_status(httpd_req_t*, const char*);
esp_err_t httpd_resp_set_type(httpd_req_t*, const char*);
esp_err_t httpd_resp_set_hdr(httpd_req_t*, const char*, const char*);
esp_err_t httpd_resp_send_chunk(httpd_req_t*, const char*, std::size_t);
