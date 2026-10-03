#include "note4_wifi_http.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <new>

#include "esp_http_client.h"
#include "esp_transport.h"
#include "esp_timer.h"

namespace note4::connectivity {

struct WifiHttpClient::Impl {
    esp_http_client_handle_t client = nullptr;
    esp_transport_handle_t transport = nullptr;
    WifiHttpStream* stream = nullptr;
    WifiHttpResponse response;
    WifiDriverResult result = WifiDriverResult::kPending;
    std::array<uint8_t, 512> request{};
    std::size_t request_size = 0;
    std::size_t request_sent = 0;
    std::size_t read_budget = 0;
    bool closed = false;
    bool eof = false;
    bool finished = false;
    std::array<char, 64> host{};
    std::array<char, 144> url{};

    static Impl* Context(esp_transport_handle_t transport) {
        return static_cast<Impl*>(esp_transport_get_context_data(transport));
    }

    static int Connect(esp_transport_handle_t transport, const char* host,
                       int port, int) {
        const auto* self = Context(transport);
        return !self->closed && port == 443 && host != nullptr &&
            std::strcmp(host, self->host.data()) == 0 ? 1 : -1;
    }

    static int CloseTransport(esp_transport_handle_t transport) {
        Context(transport)->closed = true;
        return 0;
    }

    static int DestroyTransport(esp_transport_handle_t) { return 0; }

    static int Write(esp_transport_handle_t transport, const char* data,
                     int length, int) {
        auto* self = Context(transport);
        if (self->closed || data == nullptr || length <= 0 ||
            static_cast<std::size_t>(length) > self->request.size() - self->request_size) {
            errno = EIO;
            return -1;
        }
        // Buffer the small GET so ESP-IDF cannot replay a partially written
        // header when the non-blocking TLS socket returns WANT_WRITE.
        std::memcpy(self->request.data() + self->request_size, data, length);
        self->request_size += static_cast<std::size_t>(length);
        return length;
    }

    static int Pending() {
        errno = EAGAIN;
        return ERR_TCP_TRANSPORT_CONNECTION_TIMEOUT;
    }

    static int Read(esp_transport_handle_t transport, char* data,
                    int capacity, int) {
        auto* self = Context(transport);
        if (self->closed || data == nullptr || capacity <= 0) {
            errno = ECONNRESET;
            return ERR_TCP_TRANSPORT_CONNECTION_FAILED;
        }
        if (self->request_sent < self->request_size) {
            const std::size_t remaining = self->request_size - self->request_sent;
            const int sent = self->stream->Write(
                self->request.data() + self->request_sent, remaining);
            if (sent == WifiHttpStream::kWouldBlock) return Pending();
            if (sent <= 0 || static_cast<std::size_t>(sent) > remaining) {
                self->result = WifiDriverResult::kTransferFailure;
                return ERR_TCP_TRANSPORT_CONNECTION_FAILED;
            }
            self->request_sent += static_cast<std::size_t>(sent);
            if (self->request_sent != self->request_size) return Pending();
        }
        if (self->read_budget == 0) return Pending();
        const std::size_t count = std::min(self->read_budget,
                                           static_cast<std::size_t>(capacity));
        const int received = self->stream->Read(reinterpret_cast<uint8_t*>(data), count);
        if (received == WifiHttpStream::kWouldBlock) return Pending();
        if (received < 0 || static_cast<std::size_t>(received) > count) {
            self->result = WifiDriverResult::kTransferFailure;
            return ERR_TCP_TRANSPORT_CONNECTION_FAILED;
        }
        if (received == 0) {
            self->eof = true;
            self->result = self->response.EndOfStream();
            return ERR_TCP_TRANSPORT_CONNECTION_CLOSED_BY_FIN;
        }
        self->read_budget -= static_cast<std::size_t>(received);
        self->result = self->response.Feed(
            reinterpret_cast<const uint8_t*>(data), received, esp_timer_get_time());
        if (self->result != WifiDriverResult::kPending &&
            self->result != WifiDriverResult::kReady) {
            // Validate before IDF sees the bytes, bounding its header buffers
            // and stopping malformed or oversized responses immediately.
            errno = EPROTO;
            return ERR_TCP_TRANSPORT_CONNECTION_FAILED;
        }
        return received;
    }
};

WifiHttpClient::WifiHttpClient() : impl_(new (std::nothrow) Impl()) {}

WifiHttpClient::~WifiHttpClient() {
    Close();
    delete impl_;
}

bool WifiHttpClient::Begin(WifiHttpStream& stream, uint8_t* body,
                            std::size_t capacity, const char* host, const char* path, bool binary_page, const char* token, const PageTelemetry& telemetry) {
    if (impl_ == nullptr || impl_->client != nullptr ||
        !host || !path || std::strlen(host) >= impl_->host.size() || std::strlen(path) >= 64 ||
        !impl_->response.Begin(body, capacity, binary_page)) return false;
    std::snprintf(impl_->host.data(), impl_->host.size(), "%s", host);
    std::snprintf(impl_->url.data(), impl_->url.size(), "https://%s%s", host, path);
    impl_->stream = &stream;
    impl_->transport = esp_transport_init();
    if (impl_->transport == nullptr) return false;
    esp_transport_set_context_data(impl_->transport, impl_);
    esp_transport_set_default_port(impl_->transport, 443);
    esp_transport_set_func(impl_->transport, nullptr, &Impl::Read, &Impl::Write,
                           &Impl::CloseTransport, nullptr, nullptr,
                           &Impl::DestroyTransport);
    esp_transport_set_async_connect_func(impl_->transport, &Impl::Connect);
    esp_http_client_config_t config{};
    config.url = impl_->url.data();
    config.method = HTTP_METHOD_GET;
    config.transport_type = HTTP_TRANSPORT_OVER_SSL;
    config.transport = impl_->transport;
    config.is_async = true;
    config.timeout_ms = 20;
    config.disable_auto_redirect = true;
    config.max_authorization_retries = -1;
    config.buffer_size = 512;
    config.buffer_size_tx = 512;
    config.user_agent = "Note4Platform/2";
    impl_->client = esp_http_client_init(&config);
    if (impl_->client == nullptr) { Close(); return false; }
    if (esp_http_client_set_header(impl_->client, "Accept", binary_page ? "application/octet-stream" : "text/plain") != ESP_OK ||
        esp_http_client_set_header(impl_->client, "Accept-Encoding", "identity") != ESP_OK ||
        esp_http_client_set_header(impl_->client, "Connection", "close") != ESP_OK) {
        Close(); return false;
    }
    if (token && *token) {
        if (!binary_page || std::strlen(token) > 64) { Close(); return false; }
        for (const char* p = token; *p; ++p) if (*p < 0x21 || *p > 0x7e) { Close(); return false; }
        char authorization[72]{};
        std::snprintf(authorization, sizeof(authorization), "Bearer %s", token);
        const auto saved = esp_http_client_set_header(impl_->client, "Authorization", authorization);
        volatile char* clear = authorization;
        for (std::size_t i = 0; i < sizeof(authorization); ++i) clear[i] = 0;
        if (saved != ESP_OK) { Close(); return false; }
    }
    if (binary_page && telemetry.valid) {
        if (telemetry.battery > 100 || telemetry.interval_seconds < 300 || telemetry.interval_seconds > 86400) { Close(); return false; }
        char value[16];
        std::snprintf(value, sizeof(value), "%u", telemetry.battery);
        auto saved = esp_http_client_set_header(impl_->client, "X-Note4-Battery", value);
        std::snprintf(value, sizeof(value), "%u", telemetry.millivolts);
        saved |= esp_http_client_set_header(impl_->client, "X-Note4-Millivolts", value);
        saved |= esp_http_client_set_header(impl_->client, "X-Note4-Charging", telemetry.charging ? "1" : "0");
        std::snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(telemetry.interval_seconds));
        saved |= esp_http_client_set_header(impl_->client, "X-Note4-Interval", value);
        if (saved != ESP_OK) { Close(); return false; }
    }
    return true;
}

WifiDriverResult WifiHttpClient::Poll(std::size_t* body_size) {
    if (body_size == nullptr || impl_ == nullptr || impl_->client == nullptr) {
        return WifiDriverResult::kUnavailable;
    }
    *body_size = 0;
    if (!impl_->finished) {
        impl_->read_budget = 512;
        const esp_err_t error = esp_http_client_perform(impl_->client);
        if (impl_->result == WifiDriverResult::kPending ||
            impl_->result == WifiDriverResult::kReady) {
            if (error == ESP_ERR_HTTP_EAGAIN) return WifiDriverResult::kPending;
            const bool complete = impl_->result == WifiDriverResult::kReady &&
                esp_http_client_get_status_code(impl_->client) == 200;
            // IDF 5.5 reports an incomplete body for async, close-delimited
            // responses. Accept FIN only after our bounded reader validates
            // the entire response, including UTF-8 and framing.
            const bool complete_at_eof = complete && impl_->eof &&
                (error == ESP_ERR_HTTP_INCOMPLETE_DATA ||
                 error == ESP_ERR_HTTP_CONNECTION_CLOSED);
            if (error != ESP_OK && !complete_at_eof) {
                impl_->result = WifiDriverResult::kTransferFailure;
            } else if (!complete) {
                impl_->result = WifiDriverResult::kInvalidResponse;
            }
        }
        impl_->finished = true;
    }
    if (impl_->result == WifiDriverResult::kReady) *body_size = impl_->response.BodySize();
    return impl_->result;
}

void WifiHttpClient::Close() {
    if (impl_ == nullptr) return;
    if (impl_->client != nullptr) {
        esp_http_client_cleanup(impl_->client);
        impl_->client = nullptr;
    }
    // The custom transport is caller-owned; IDF does not destroy it.
    if (impl_->transport != nullptr) esp_transport_destroy(impl_->transport);
    *impl_ = {};
}

bool WifiHttpClient::ClockSample(time::TimeSample* sample) const {
    return impl_ && impl_->finished && impl_->result == WifiDriverResult::kReady &&
        impl_->response.ClockSample(sample);
}

}  // namespace note4::connectivity
