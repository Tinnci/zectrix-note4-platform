#include "zectrix_wifi_http.h"
#include "esp_http_client.h"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <string>

using namespace zectrix::connectivity;

// The HTTP fake drives the registered transport callbacks. Wire validation
// uses the production reader; this fake supplies only IDF completion signals.
struct esp_transport {
    void* context = nullptr;
    transport_connect connect = nullptr;
    transport_read read = nullptr;
    transport_write write = nullptr;
    transport_close close = nullptr;
    transport_destroy destroy = nullptr;
};

struct esp_http_client {
    esp_transport_handle_t transport;
    std::string headers;
    std::size_t received = 0;
    bool sent = false;
};

static int live_transports = 0;
static int live_clients = 0;
static int perform_calls = 0;
static std::size_t complete_after_bytes = 0;

esp_transport_handle_t esp_transport_init() {
    ++live_transports;
    return new esp_transport{};
}
esp_err_t esp_transport_destroy(esp_transport_handle_t transport) {
    if (transport->destroy != nullptr) transport->destroy(transport);
    delete transport;
    --live_transports;
    return ESP_OK;
}
void* esp_transport_get_context_data(esp_transport_handle_t transport) {
    return transport->context;
}
esp_err_t esp_transport_set_context_data(esp_transport_handle_t transport, void* context) {
    transport->context = context;
    return ESP_OK;
}
esp_err_t esp_transport_set_default_port(esp_transport_handle_t, int port) {
    assert(port == 443);
    return ESP_OK;
}
esp_err_t esp_transport_set_async_connect_func(esp_transport_handle_t transport,
                                              transport_connect connect) {
    transport->connect = connect;
    return ESP_OK;
}
esp_err_t esp_transport_set_func(esp_transport_handle_t transport,
    transport_connect, transport_read read, transport_write write,
    transport_close close, transport_poll, transport_poll, transport_destroy destroy) {
    transport->read = read;
    transport->write = write;
    transport->close = close;
    transport->destroy = destroy;
    return ESP_OK;
}

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* config) {
    assert(std::strcmp(config->url, "https://zectrix.com/robots.txt") == 0);
    assert(config->is_async && config->disable_auto_redirect);
    assert(config->max_authorization_retries < 0 && config->buffer_size <= 512);
    ++live_clients;
    return new esp_http_client{config->transport, {}, 0, false};
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client,
                                    const char* key, const char* value) {
    client->headers += std::string(key) + ": " + value + "\r\n";
    return ESP_OK;
}
esp_err_t esp_http_client_perform(esp_http_client_handle_t client) {
    ++perform_calls;
    const auto transport = client->transport;
    if (!client->sent) {
        assert(transport->connect(transport, "zectrix.com", 443, 20) == 1);
        assert(transport->connect(transport, "example.com", 443, 20) < 0);
        const std::string request = "GET /robots.txt HTTP/1.1\r\nHost: zectrix.com\r\n" +
            client->headers + "\r\n";
        assert(transport->write(transport, request.data(), 17, 20) == 17);
        const auto rest = static_cast<int>(request.size() - 17);
        assert(transport->write(transport, request.data() + 17, rest, 20) == rest);
        client->sent = true;
    }
    char bytes[512];
    while (true) {
        errno = 0;
        const int received = transport->read(transport, bytes, sizeof(bytes), 20);
        if (received == ERR_TCP_TRANSPORT_CONNECTION_TIMEOUT) {
            assert(errno == EAGAIN);
            return ESP_ERR_HTTP_EAGAIN;
        }
        if (received == ERR_TCP_TRANSPORT_CONNECTION_CLOSED_BY_FIN) {
            return ESP_ERR_HTTP_INCOMPLETE_DATA;
        }
        if (received < 0) return ESP_FAIL;
        client->received += static_cast<std::size_t>(received);
        if (complete_after_bytes != 0 && client->received >= complete_after_bytes) {
            transport->close(transport);
            return ESP_OK;
        }
    }
}
int esp_http_client_get_status_code(esp_http_client_handle_t) { return 200; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) {
    client->transport->close(client->transport);
    delete client;
    --live_clients;
    return ESP_OK;
}

namespace {

const std::string kHeaders = "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n";

WifiDriverResult ReadResponse(const std::string& wire, std::size_t fragment,
                              std::size_t capacity = 2048) {
    WifiHttpResponse response;
    std::array<uint8_t, 2050> body{};
    body.front() = 0xa5;
    body[capacity + 1] = 0x5a;
    assert(response.Begin(body.data() + 1, capacity));
    WifiDriverResult result = WifiDriverResult::kPending;
    for (std::size_t offset = 0; offset < wire.size(); offset += fragment) {
        result = response.Feed(reinterpret_cast<const uint8_t*>(wire.data() + offset),
                               std::min(fragment, wire.size() - offset));
        if (result != WifiDriverResult::kPending && result != WifiDriverResult::kReady) break;
    }
    if (result == WifiDriverResult::kPending) result = response.EndOfStream();
    assert(body.front() == 0xa5 && body[capacity + 1] == 0x5a);
    if (result == WifiDriverResult::kReady) assert(response.BodySize() != 0);
    return result;
}

void TestFramingAndValidation() {
    const std::string successes[] = {
        kHeaders + "Content-Length: 3\r\n\r\nok\n",
        kHeaders + "Connection: close\r\n\r\nok\n",
        kHeaders + "Transfer-Encoding: chunked\r\n\r\n1\r\no\r\n2\r\nk\n\r\n0\r\n\r\n",
        kHeaders + "Transfer-Encoding: chunked\r\n\r\n3;x=y\r\nok\n\r\n0\r\nX-Note: end\r\n\r\n",
        "HTTP/1.0 200 OK\r\ncOnTeNt-TyPe: TEXT/PLAIN; charset=\"UTF-8\"\r\n\r\n\xe4\xbd\xa0",
    };
    for (const auto& wire : successes) {
        for (std::size_t fragment = 1; fragment <= wire.size(); ++fragment) {
            assert(ReadResponse(wire, fragment) == WifiDriverResult::kReady);
        }
    }
    const std::string invalid[] = {
        "HTTP/1.1 302 Found\r\nLocation: https://example.com\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nok\n",
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\nhi",
        kHeaders + "Content-Length: 0\r\n\r\n",
        kHeaders + "Content-Length: 3\r\n\r\nok",
        kHeaders + "Content-Length: 2\r\n\r\nok\n",
        kHeaders + "Content-Length: 3\r\nContent-Length: 3\r\n\r\nok\n",
        kHeaders + "Content-Length: 3\r\nTransfer-Encoding: chunked\r\n\r\nok\n",
        kHeaders + "Content-Encoding: gzip\r\n\r\nzip",
        kHeaders + "Transfer-Encoding: gzip, chunked\r\n\r\n",
        kHeaders + "Content-Type: text/plain\r\n\r\nok\n",
        kHeaders + "Content-Length: -3\r\n\r\n",
        kHeaders + "Content-Length : 3\r\n\r\nok\n",
        kHeaders + "\n\nok\n",
        kHeaders + "Transfer-Encoding: chunked\r\n\r\n3\r\nok\n\r\n",
        kHeaders + "Transfer-Encoding: chunked\r\n\r\n3\r\nok\nx\n0\r\n\r\n",
        kHeaders + "Transfer-Encoding: chunked\r\n\r\n3\r\nok\n\r\n0\r\nContent-Length: 3\r\n\r\n",
        kHeaders + "\r\n\xc0\x80",
        kHeaders + "\r\n\xed\xa0\x80",
        kHeaders + "\r\n\xf4\x90\x80\x80",
        kHeaders + "\r\n\xe4\xbd",
        kHeaders + "X-Huge: " + std::string(512, 'x') + "\r\n\r\nok\n",
    };
    for (const auto& wire : invalid) {
        assert(ReadResponse(wire, 1) == WifiDriverResult::kInvalidResponse);
        assert(ReadResponse(wire, 512) == WifiDriverResult::kInvalidResponse);
    }
    assert(ReadResponse("HTTP/1.1 503 Unavailable\r\n\r\n", 1) == WifiDriverResult::kServerError);
    for (const auto& framing : {std::string("Content-Length: 2048\r\n\r\n"),
                                std::string("Connection: close\r\n\r\n")}) {
        assert(ReadResponse(kHeaders + framing + std::string(2048, 'a'), 13) == WifiDriverResult::kReady);
    }
    for (const auto& wire : {
        kHeaders + "Content-Length: 999999999999999999999999\r\n\r\n",
        kHeaders + "Transfer-Encoding: chunked\r\n\r\nffffffffffffffffffff\r\n",
        kHeaders + "Connection: close\r\n\r\n" + std::string(2049, 'a')}) {
        assert(ReadResponse(wire, 7) == WifiDriverResult::kResponseTooLarge);
    }
    std::string many_headers = kHeaders;
    for (int i = 0; i < 600; ++i) many_headers += "X: abc\r\n";
    assert(ReadResponse(many_headers + "\r\nok\n", 32) == WifiDriverResult::kInvalidResponse);
}

class Stream final : public WifiHttpStream {
public:
    int Read(uint8_t* data, std::size_t capacity) override {
        ++reads;
        if (reads % 3 == 0) return kWouldBlock;
        if (read_failure) return kFailure;
        const auto count = std::min({capacity, response.size() - offset, fragment});
        std::memcpy(data, response.data() + offset, count);
        offset += count;
        return static_cast<int>(count);
    }
    int Write(const uint8_t* data, std::size_t size) override {
        ++writes;
        if (writes % 2 == 1) return kWouldBlock;
        if (write_failure) return kFailure;
        const auto count = std::min(size, std::size_t{11});
        request.append(reinterpret_cast<const char*>(data), count);
        return static_cast<int>(count);
    }
    std::string response;
    std::string request;
    std::size_t offset = 0;
    std::size_t fragment = 512;
    int reads = 0;
    int writes = 0;
    bool read_failure = false;
    bool write_failure = false;
};

void TestNonBlockingClientAndReuse() {
    WifiHttpClient client;
    for (int scenario = 0; scenario < 5; ++scenario) {
        Stream stream;
        stream.response = kHeaders + (scenario == 1 ? "\r\n" : "Content-Length: 1500\r\n\r\n") +
            std::string(1500, 'a');
        stream.read_failure = scenario == 2;
        stream.write_failure = scenario == 3;
        complete_after_bytes = scenario == 1 ? 0 : stream.response.size();
        std::array<uint8_t, 2048> body{};
        assert(client.Begin(stream, body.data(), scenario == 4 ? 8 : body.size()));
        assert(!client.Begin(stream, body.data(), body.size()));
        std::size_t size = 99;
        WifiDriverResult result = WifiDriverResult::kPending;
        for (int poll = 0; poll < 200 && result == WifiDriverResult::kPending; ++poll) {
            const auto before = stream.offset;
            result = client.Poll(&size);
            assert(stream.offset - before <= 512);
        }
        const auto expected = scenario < 2 ? WifiDriverResult::kReady :
            scenario == 4 ? WifiDriverResult::kResponseTooLarge : WifiDriverResult::kTransferFailure;
        assert(result == expected);
        assert(size == (scenario < 2 ? 1500 : 0));
        if (scenario != 3) {
            assert(stream.request.find("GET /robots.txt HTTP/1.1\r\n") == 0);
            assert(stream.request.find("GET ", 1) == std::string::npos);
            assert(stream.request.find("Accept: text/plain\r\n") != std::string::npos);
            assert(stream.request.find("Accept-Encoding: identity\r\n") != std::string::npos);
        }
        const auto calls = perform_calls;
        assert(client.Poll(&size) == result && perform_calls == calls);
        client.Close();
        client.Close();
        assert(live_transports == 0 && live_clients == 0);
    }
}

}  // namespace

int main() {
    TestFramingAndValidation();
    TestNonBlockingClientAndReuse();
}
