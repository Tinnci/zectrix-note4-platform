#include "zectrix_book_web.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <algorithm>
#include <mutex>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_http_server.h"
#include "esp_timer.h"

namespace zectrix::connectivity {
namespace {
const char* Status(int status) {
    switch (status) {
        case 200: return "200 OK";
        case 400: return "400 Bad Request";
        case 401: return "401 Unauthorized";
        case 404: return "404 Not Found";
        case 405: return "405 Method Not Allowed";
        case 408: return "408 Request Timeout";
        case 409: return "409 Conflict";
        case 413: return "413 Content Too Large";
        case 503: return "503 Service Unavailable";
        case 507: return "507 Insufficient Storage";
        default: return "500 Internal Server Error";
    }
}

class EspRequest final : public BookHttpRequest {
public:
    explicit EspRequest(httpd_req_t* request) : request_(request) {
        uri = request->uri;
        content_length = request->content_len;
        method = request->method == HTTP_GET ? BookHttpMethod::Get : request->method == HTTP_PUT ? BookHttpMethod::Put :
            request->method == HTTP_DELETE ? BookHttpMethod::Delete : request->method == HTTP_POST ? BookHttpMethod::Post :
            BookHttpMethod::Other;
    }
    bool Header(const char* name, char* value, std::size_t capacity) override {
        return httpd_req_get_hdr_value_len(request_, name) < capacity &&
            httpd_req_get_hdr_value_str(request_, name, value, capacity) == ESP_OK;
    }
    int Read(uint8_t* output, std::size_t capacity) override {
        const int read = httpd_req_recv(request_, reinterpret_cast<char*>(output), capacity);
        if (read > 0) consumed += read;
        return read;
    }
    bool Respond(int status, const char* type, const char* attachment) override {
        if (httpd_resp_set_status(request_, Status(status)) != ESP_OK ||
            httpd_resp_set_type(request_, type) != ESP_OK) return false;
        const char* headers[][2] = {
            {"Cache-Control", "no-store"}, {"Connection", "close"}, {"X-Content-Type-Options", "nosniff"},
            {"Referrer-Policy", "no-referrer"}, {"X-Frame-Options", "DENY"},
            {"Content-Security-Policy", "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; img-src data:; base-uri 'none'; frame-ancestors 'none'; form-action 'none'"},
        };
        for (const auto& header : headers)
            if (httpd_resp_set_hdr(request_, header[0], header[1]) != ESP_OK) return false;
        if (attachment) {
            std::strcpy(disposition_.data(), "attachment; filename*=UTF-8''");
            std::size_t offset = std::strlen(disposition_.data());
            for (const auto* p = reinterpret_cast<const uint8_t*>(attachment); *p; ++p) {
                std::snprintf(disposition_.data() + offset, disposition_.size() - offset, "%%%02X", *p);
                offset += 3;
            }
            if (httpd_resp_set_hdr(request_, "Content-Disposition", disposition_.data()) != ESP_OK) return false;
        }
        return true;
    }
    bool Write(const char* data, std::size_t size) override { return httpd_resp_send_chunk(request_, data, size) == ESP_OK; }
    bool Finish() override { return httpd_resp_send_chunk(request_, nullptr, 0) == ESP_OK; }
    uint32_t NowMs() const override { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
    void DrainRejectedBody() {
        if (consumed == content_length) return;
        // Send FIN after the error response, then briefly drain incoming bytes.
        // Immediate close with unread data can reset TCP and hide the response.
        shutdown(httpd_req_to_sockfd(request_), SHUT_WR);
        const auto started = NowMs();
        std::array<uint8_t, 512> discarded{};
        while (consumed < content_length && NowMs() - started < 500) {
            if (Read(discarded.data(), std::min(discarded.size(), content_length - consumed)) <= 0) break;
        }
    }
    std::size_t consumed = 0;
private:
    httpd_req_t* request_;
    std::array<char, 256> disposition_{};
};
}  // namespace

struct EspBookWebServer::Impl {
    httpd_handle_t server = nullptr;
    BookWebApi api;
    std::mutex clients_mutex;
    std::array<int, 4> clients{{-1, -1, -1, -1}};
    static esp_err_t Open(httpd_handle_t server, int socket) {
        auto& self = *static_cast<Impl*>(httpd_get_global_user_ctx(server));
        std::lock_guard<std::mutex> lock(self.clients_mutex);
        if (self.api.Cancelled()) return ESP_FAIL;
        for (auto& client : self.clients) {
            if (client < 0) { client = socket; return ESP_OK; }
        }
        return ESP_ERR_NO_MEM;
    }
    static void Close(httpd_handle_t server, int socket) {
        auto& self = *static_cast<Impl*>(httpd_get_global_user_ctx(server));
        std::lock_guard<std::mutex> lock(self.clients_mutex);
        for (auto& client : self.clients) if (client == socket) client = -1;
        close(socket);
    }
    static esp_err_t Handle(httpd_req_t* request) {
        auto& self = *static_cast<Impl*>(request->user_ctx);
        EspRequest adapter(request);
        const bool sent = self.api.Handle(adapter);
        adapter.DrainRejectedBody();
        // Unfinished bodies close after a bounded drain instead of IDF's full purge.
        return sent && adapter.consumed == adapter.content_length ? ESP_OK : ESP_FAIL;
    }
};

EspBookWebServer::EspBookWebServer() : impl_(new (std::nothrow) Impl) {}
EspBookWebServer::~EspBookWebServer() { if (Stop()) delete impl_; }

bool EspBookWebServer::Start(storage::BookStorage& books, const char* code, uint32_t now_ms) {
    if (!impl_ || impl_->server) return false;
    impl_->api.Start(books, code, now_ms);
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_open_sockets = 4;
    config.max_uri_handlers = 4;
    config.recv_wait_timeout = config.send_wait_timeout = 1;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.global_user_ctx = impl_;
    config.global_user_ctx_free_fn = [](void*) {};
    config.open_fn = Impl::Open;
    config.close_fn = Impl::Close;
    if (httpd_start(&impl_->server, &config) != ESP_OK) return false;
    for (const auto method : {HTTP_GET, HTTP_PUT, HTTP_DELETE, HTTP_POST}) {
        httpd_uri_t uri{};
        uri.uri = "/*";
        uri.method = method;
        uri.handler = Impl::Handle;
        uri.user_ctx = impl_;
        if (httpd_register_uri_handler(impl_->server, &uri) != ESP_OK) { Stop(); return false; }
    }
    return true;
}

BookTransferProgress EspBookWebServer::Progress() const { return impl_ ? impl_->api.Progress() : BookTransferProgress{}; }

bool EspBookWebServer::Stop() {
    if (!impl_) return true;
    impl_->api.Cancel();
    {
        std::lock_guard<std::mutex> lock(impl_->clients_mutex);
        // Wake header parsing and slow uploads before joining the HTTP task.
        for (const auto client : impl_->clients) if (client >= 0) shutdown(client, SHUT_RDWR);
    }
    if (impl_->server && httpd_stop(impl_->server) != ESP_OK) return false;
    impl_->server = nullptr;
    return true;
}

}  // namespace zectrix::connectivity
