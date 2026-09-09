#include "zectrix_book_web.h"
#include "zectrix_book_storage.h"

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>

namespace {
using namespace zectrix::connectivity;
volatile std::sig_atomic_t interrupted = 0;
void Interrupt(int) { interrupted = 1; }
uint32_t Now() {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
bool Send(int socket, const char* data, std::size_t size) {
    while (size) {
        const auto sent = send(socket, data, size, 0);
        if (sent <= 0) return false;
        data += sent; size -= sent;
    }
    return true;
}

class SocketRequest final : public BookHttpRequest {
public:
    explicit SocketRequest(int socket) : socket_(socket) {}
    bool Parse() {
        std::string headers;
        while (headers.size() < 8192 && (headers.size() < 4 || headers.compare(headers.size() - 4, 4, "\r\n\r\n") != 0)) {
            char byte;
            if (recv(socket_, &byte, 1, 0) != 1) return false;
            headers += byte;
        }
        if (headers.size() == 8192) return false;
        std::istringstream input(headers);
        std::string verb, version, line;
        if (!(input >> verb >> path_ >> version) || path_.size() > 256 || version != "HTTP/1.1") return false;
        uri = path_.c_str();
        method = verb == "GET" ? BookHttpMethod::Get : verb == "PUT" ? BookHttpMethod::Put :
            verb == "DELETE" ? BookHttpMethod::Delete : verb == "POST" ? BookHttpMethod::Post : BookHttpMethod::Other;
        std::getline(input, line);
        while (std::getline(input, line) && line != "\r") {
            const auto colon = line.find(':');
            if (colon == std::string::npos || line.empty() || line.back() != '\r') return false;
            auto name = line.substr(0, colon);
            for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            auto value = line.substr(colon + 1, line.size() - colon - 2);
            value.erase(0, value.find_first_not_of(' '));
            if (!headers_.emplace(name, value).second) return false;
        }
        if (headers_.count("transfer-encoding")) return false;
        if (const auto length = headers_.find("content-length"); length != headers_.end()) {
            const auto& value = length->second;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), content_length);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) return false;
        }
        return true;
    }
    bool Header(const char* name, char* value, std::size_t capacity) override {
        std::string key(name);
        for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const auto found = headers_.find(key);
        if (found == headers_.end() || found->second.size() >= capacity) return false;
        std::strcpy(value, found->second.c_str()); return true;
    }
    int Read(uint8_t* output, std::size_t capacity) override {
        const auto size = recv(socket_, output, capacity, 0);
        if (size > 0) consumed_ += size;
        return size;
    }
    void DrainRejectedBody() {
        if (consumed_ == content_length) return;
        shutdown(socket_, SHUT_WR);
        const auto started = Now();
        std::array<uint8_t, 512> discarded{};
        while (consumed_ < content_length && Now() - started < 500)
            if (Read(discarded.data(), std::min(discarded.size(), content_length - consumed_)) <= 0) break;
    }
    bool Respond(int status, const char* type, const char* attachment) override {
        std::string headers = "HTTP/1.1 " + std::to_string(status) + " Response\r\nContent-Type: " + type +
            "\r\nTransfer-Encoding: chunked\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
            "Content-Security-Policy: default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; img-src data:; base-uri 'none'; frame-ancestors 'none'; form-action 'none'\r\n";
        if (attachment) {
            headers += "Content-Disposition: attachment; filename*=UTF-8''";
            for (const auto* p = reinterpret_cast<const uint8_t*>(attachment); *p; ++p) {
                char escaped[4]; std::snprintf(escaped, sizeof(escaped), "%%%02X", *p); headers += escaped;
            }
            headers += "\r\n";
        }
        headers += "\r\n";
        return Send(socket_, headers.data(), headers.size());
    }
    bool Write(const char* data, std::size_t size) override {
        if (!size) return true;
        char chunk[32]; const int length = std::snprintf(chunk, sizeof(chunk), "%zx\r\n", size);
        return Send(socket_, chunk, length) && Send(socket_, data, size) && Send(socket_, "\r\n", 2);
    }
    bool Finish() override { return Send(socket_, "0\r\n\r\n", 5); }
    uint32_t NowMs() const override { return Now(); }
private:
    int socket_;
    std::size_t consumed_ = 0;
    std::string path_;
    std::unordered_map<std::string, std::string> headers_;
};

class HostServer final : public BookTransferServer {
public:
    explicit HostServer(uint16_t port) : port_(port) {}
    ~HostServer() override { Stop(); }
    bool Start(zectrix::storage::BookStorage& books, const char* code, uint32_t now) override {
        listener_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listener_ < 0) return false;
        sockaddr_in address{};
        address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(port_);
        if (bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(listener_, 4) != 0) return false;
        socklen_t size = sizeof(address);
        if (getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &size) != 0) return false;
        api_.Start(books, code, now);
        stopping_ = false;
        worker_ = std::thread([this] {
            while (!stopping_.load()) {
                pollfd pending{listener_, POLLIN, 0};
                if (poll(&pending, 1, 50) <= 0) continue;
                const int client = accept(listener_, nullptr, nullptr);
                if (client < 0) continue;
                {
                    std::lock_guard<std::mutex> lock(client_mutex_);
                    if (stopping_.load()) { close(client); break; }
                    client_ = client;
                }
                timeval timeout{1, 0};
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
                setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
                SocketRequest request(client);
                if (request.Parse()) { api_.Handle(request); request.DrainRejectedBody(); }
                {
                    std::lock_guard<std::mutex> lock(client_mutex_);
                    close(client);
                    client_ = -1;
                }
            }
        });
        std::printf("READY http://127.0.0.1:%u/ code=%s\n", ntohs(address.sin_port), code);
        std::fflush(stdout);
        return true;
    }
    BookTransferProgress Progress() const override { return api_.Progress(); }
    bool Stop() override {
        stopping_ = true; api_.Cancel();
        {
            std::lock_guard<std::mutex> lock(client_mutex_);
            if (client_ >= 0) shutdown(client_, SHUT_RDWR);
        }
        if (worker_.joinable()) worker_.join();
        if (listener_ >= 0) close(listener_);
        listener_ = -1;
        return true;
    }
private:
    uint16_t port_;
    int listener_ = -1;
    BookWebApi api_;
    std::atomic<bool> stopping_{false};
    std::thread worker_;
    std::mutex client_mutex_;
    int client_ = -1;
};
class HostRadio final : public BookTransferRadio {
public:
    WifiDriverResult Start(BookTransferMode, const WifiCredentials&) override { return WifiDriverResult::kPending; }
    WifiDriverResult Poll(BookTransferMode, char* address, std::size_t) override {
        std::strcpy(address, "127.0.0.1"); return WifiDriverResult::kReady;
    }
    WifiDriverResult Stop() override { return WifiDriverResult::kReady; }
};
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) { std::fprintf(stderr, "Usage: book-web-host BOOK_DIRECTORY [PORT]\n"); return 2; }
    unsigned port = 0;
    if (argc == 3) {
        const auto* end = argv[2] + std::strlen(argv[2]);
        const auto parsed = std::from_chars(argv[2], end, port);
        if (parsed.ec != std::errc{} || parsed.ptr != end || port > 65535) return 2;
    }
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, Interrupt); std::signal(SIGTERM, Interrupt);
    zectrix::storage::BookStorage books(argv[1]);
    if (books.BeginManagement() != ESP_OK) { std::fprintf(stderr, "Cannot open the book directory.\n"); return 1; }
    HostRadio radio; HostServer server(port); BookTransfer transfer(radio, server);
    WifiCredentials credentials;
    transfer.Begin(books, BookTransferMode::Hotspot, credentials, "ABCDEFGH2345", Now());
    while (!interrupted && transfer.Busy()) {
        transfer.Poll(Now());
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    transfer.Stop();
    const auto snapshot = transfer.Snapshot();
    std::printf("STOPPED uploaded=%u\n", snapshot.uploaded);
    return snapshot.state == BookTransferState::Failed ? 1 : 0;
}
