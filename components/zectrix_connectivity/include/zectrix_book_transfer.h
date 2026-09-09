#pragma once

#include "zectrix_wifi_backend.h"

namespace zectrix::storage { class BookStorage; }

namespace zectrix::connectivity {

enum class BookTransferMode : uint8_t { Hotspot, Station };
enum class BookTransferState : uint8_t { Off, Starting, Sharing, Stopping, Complete, Failed };
enum class BookTransferError : uint8_t { None, Storage, Wifi, Server, Timeout, Power, Policy, Stop, Busy, Credentials };

struct BookTransferProgress {
    uint32_t received = 0, expected = 0, uploaded = 0;
    uint32_t activity_ms = 0, completed_ms = 0;
    bool active = false, finish = false;
};

struct BookTransferSnapshot {
    BookTransferState state = BookTransferState::Off;
    BookTransferMode mode = BookTransferMode::Hotspot;
    BookTransferError error = BookTransferError::None;
    std::array<char, 33> ssid{};
    std::array<char, 13> code{};
    std::array<char, 16> address{};
    uint32_t received = 0, expected = 0, uploaded = 0, seconds_left = 0;
};

class BookTransferRadio {
public:
    virtual ~BookTransferRadio() = default;
    virtual WifiDriverResult Start(BookTransferMode mode, const WifiCredentials& credentials) = 0;
    virtual WifiDriverResult Poll(BookTransferMode mode, char* address, std::size_t capacity) = 0;
    virtual WifiDriverResult Stop() = 0;
};

class BookTransferServer {
public:
    virtual ~BookTransferServer() = default;
    virtual bool Start(storage::BookStorage& books, const char* code, uint32_t now_ms) = 0;
    virtual BookTransferProgress Progress() const = 0;
    // Cancellation joins all handlers before releasing the Storage lease.
    virtual bool Stop() = 0;
};

// All lifecycle methods run on the Connectivity owner under its existing mutex.
class BookTransfer {
public:
    static constexpr uint32_t kStartupMs = 20000;
    static constexpr uint32_t kIdleMs = 180000;
    static constexpr uint32_t kAfterUploadMs = 30000;
    static constexpr uint32_t kSessionMs = 900000;
    BookTransfer(BookTransferRadio& radio, BookTransferServer& server) : radio_(radio), server_(server) {}
    ~BookTransfer() { Stop(); }
    bool Begin(storage::BookStorage& books, BookTransferMode mode, const WifiCredentials& credentials,
               const char* code, uint32_t now_ms);
    void Poll(uint32_t now_ms, bool power_allowed = true, bool policy_allowed = true);
    bool Stop(BookTransferError error = BookTransferError::None);
    bool Busy() const { return books_ != nullptr; }
    BookTransferSnapshot Snapshot() const { return snapshot_; }

private:
    BookTransferRadio& radio_;
    BookTransferServer& server_;
    storage::BookStorage* books_ = nullptr;
    BookTransferSnapshot snapshot_{};
    uint32_t started_ms_ = 0, finish_ms_ = 0;
    WifiCredentials pending_credentials_{};
    bool finishing_ = false;
    bool server_started_ = false;
    bool start_pending_ = false;
};

}  // namespace zectrix::connectivity
