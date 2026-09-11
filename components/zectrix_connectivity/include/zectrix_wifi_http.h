#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "zectrix_wifi_backend.h"
#include "zectrix_time_sync.h"

namespace zectrix::connectivity {

// A borrowed, already verified TLS stream. It never blocks for network I/O.
class WifiHttpStream {
public:
    static constexpr int kWouldBlock = -2;
    static constexpr int kFailure = -1;
    virtual ~WifiHttpStream() = default;
    virtual int Read(uint8_t* data, std::size_t capacity) = 0;
    virtual int Write(const uint8_t* data, std::size_t size) = 0;
};

// Uses esp_http_client for the fixed capability over a borrowed TLS stream.
// Each Poll consumes at most 512 wire bytes, including HTTP framing.
class WifiHttpClient final {
public:
    WifiHttpClient();
    ~WifiHttpClient();
    WifiHttpClient(const WifiHttpClient&) = delete;
    WifiHttpClient& operator=(const WifiHttpClient&) = delete;

    bool Begin(WifiHttpStream& stream, uint8_t* body, std::size_t capacity);
    WifiDriverResult Poll(std::size_t* body_size);
    bool ClockSample(time::TimeSample* sample) const;
    void Close();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

// A bounded HTTP/1.x response reader for the fixed, unauthenticated GET
// capability. Redirects, compression and ambiguous framing are rejected.
class WifiHttpResponse final {
public:
    static constexpr std::size_t kMaximumHeaderBytes = 4096;
    static constexpr std::size_t kMaximumHeaderLineBytes = 512;

    bool Begin(uint8_t* body, std::size_t capacity);
    WifiDriverResult Feed(const uint8_t* data, std::size_t size, int64_t received_us = 0);
    WifiDriverResult EndOfStream();
    std::size_t BodySize() const { return body_size_; }
    bool ClockSample(time::TimeSample* sample) const;

private:
    enum class State : uint8_t {
        kStatus, kHeaders, kFixedBody, kCloseBody, kChunkSize,
        kChunkBody, kChunkCr, kChunkLf, kTrailers, kDone, kFailed,
    };
    void ProcessLine();
    void ProcessHeader(bool trailer);
    void Complete();
    void Fail(WifiDriverResult result = WifiDriverResult::kInvalidResponse);
    WifiDriverResult Result() const;

    State state_ = State::kFailed;
    WifiDriverResult failure_ = WifiDriverResult::kInvalidResponse;
    std::array<char, kMaximumHeaderLineBytes + 1> line_{};
    std::size_t line_size_ = 0;
    std::size_t header_bytes_ = 0;
    uint8_t* body_ = nullptr;
    std::size_t body_capacity_ = 0;
    std::size_t body_size_ = 0;
    std::size_t remaining_ = 0;
    bool line_cr_ = false;
    bool content_type_seen_ = false;
    bool length_seen_ = false;
    bool chunked_ = false;
    time::TimeSample clock_{};
    int64_t received_us_ = 0;
    bool date_seen_ = false, date_invalid_ = false;
};

}  // namespace zectrix::connectivity
