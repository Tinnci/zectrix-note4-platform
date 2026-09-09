#include "zectrix_book_web.h"
#include "zectrix_book_storage.h"
#include "esp_http_server.h"

#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace {
struct Server {
    httpd_config_t config;
    std::vector<httpd_uri_t> handlers;
    std::vector<int> sockets;
    std::thread worker;
};
Server* current = nullptr;
bool fail_stop = false;
struct Response {
    std::string authorization = "Bearer ABCDEFGH2345";
    std::string status, type, body;
    std::vector<std::pair<std::string, const char*>> headers;
    bool finished = false;
};
Response& State(httpd_req_t* request) { return *static_cast<Response*>(request->test); }
template <typename Predicate> void Wait(Predicate done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!done()) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
}
}  // namespace

int64_t esp_timer_get_time() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool httpd_uri_match_wildcard(const char*, const char*, std::size_t) { return true; }
esp_err_t httpd_start(httpd_handle_t* output, const httpd_config_t* config) {
    assert(!current && config->open_fn && config->close_fn && config->global_user_ctx_free_fn);
    assert(config->stack_size == 8192 && config->max_open_sockets == 4 && config->recv_wait_timeout == 1);
    current = new Server{*config, {}, {}, {}};
    *output = current; return ESP_OK;
}
esp_err_t httpd_stop(httpd_handle_t handle) {
    auto* server = static_cast<Server*>(handle);
    if (fail_stop) return ESP_FAIL;
    if (server->worker.joinable()) server->worker.join();
    for (const auto socket : server->sockets) server->config.close_fn(handle, socket);
    server->config.global_user_ctx_free_fn(server->config.global_user_ctx);
    delete server; current = nullptr;
    return ESP_OK;
}
esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t* uri) {
    static_cast<Server*>(handle)->handlers.push_back(*uri); return ESP_OK;
}
void* httpd_get_global_user_ctx(httpd_handle_t handle) { return static_cast<Server*>(handle)->config.global_user_ctx; }
int httpd_req_to_sockfd(httpd_req_t* request) { return request->socket; }
std::size_t httpd_req_get_hdr_value_len(httpd_req_t* request, const char*) { return State(request).authorization.size(); }
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t* request, const char*, char* output, std::size_t capacity) {
    const auto& auth = State(request).authorization;
    if (auth.size() >= capacity) return ESP_ERR_INVALID_SIZE;
    std::strcpy(output, auth.c_str()); return ESP_OK;
}
int httpd_req_recv(httpd_req_t* request, char* output, std::size_t capacity) { return recv(request->socket, output, capacity, 0); }
esp_err_t httpd_resp_set_status(httpd_req_t* request, const char* value) { State(request).status = value; return ESP_OK; }
esp_err_t httpd_resp_set_type(httpd_req_t* request, const char* value) { State(request).type = value; return ESP_OK; }
esp_err_t httpd_resp_set_hdr(httpd_req_t* request, const char* name, const char* value) {
    State(request).headers.emplace_back(name, value); return ESP_OK;
}
esp_err_t httpd_resp_send_chunk(httpd_req_t* request, const char* data, std::size_t size) {
    auto& state = State(request);
    if (!size) state.finished = true;
    else state.body.append(data, size);
    for (const auto& header : state.headers) {
        assert(header.first != "Access-Control-Allow-Origin");
        assert(header.second && *header.second);
    }
    return ESP_OK;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    namespace fs = std::filesystem;
    const auto root = fs::path(argv[1]) / "esp-http";
    fs::create_directory(root);
    zectrix::storage::BookStorage books(root.c_str());
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        assert(books.BeginManagement() == ESP_OK);
        zectrix::connectivity::EspBookWebServer server;
        assert(server.Start(books, "ABCDEFGH2345", 0));
        assert(current->handlers.size() == 4);
        int sockets[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
        assert(current->config.open_fn(current, sockets[0]) == ESP_OK);
        current->sockets.push_back(sockets[0]);
        std::atomic<bool> entered{false};
        Response response;
        httpd_req_t request{"/api/books/cancelled.txt", HTTP_PUT, 8192, current->handlers[1].user_ctx, sockets[0], &response};
        if (scenario == 1) {
            std::string prefix(1024, 'x');
            assert(send(sockets[1], prefix.data(), prefix.size(), 0) == static_cast<ssize_t>(prefix.size()));
            current->worker = std::thread([&] { current->handlers[1].handler(&request); });
            Wait([&] { return server.Progress().received == 1024; });
            assert(fs::exists(root / ".upload.part"));
        } else {
            current->worker = std::thread([&] {
                entered = true;
                char byte;
                assert(recv(sockets[0], &byte, 1, 0) == 0);
            });
            Wait([&] { return entered.load(); });
        }
        const auto started = std::chrono::steady_clock::now();
        fail_stop = scenario == 2;
        if (fail_stop) { assert(!server.Stop()); fail_stop = false; }
        assert(server.Stop() && server.Stop());
        assert(std::chrono::steady_clock::now() - started < std::chrono::seconds(1));
        close(sockets[1]);
        assert(!current && !fs::exists(root / ".upload.part") && !fs::exists(root / "cancelled.txt"));
        assert(books.EndManagement() == ESP_OK);
    }
    std::puts("PASS: ESP HTTP adapter cancels blocked headers and uploads before joining and releasing Storage.");
}
