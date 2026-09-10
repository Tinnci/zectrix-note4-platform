#pragma once

#include "zectrix_cli_session.h"
#include "zectrix_host_channel.h"

namespace zectrix::host {

inline constexpr std::size_t kHeaderSize = 16;
inline constexpr std::size_t kWireSize = kHeaderSize + kPayloadSize +
    (kHeaderSize + kPayloadSize) / 254 + 2;
inline constexpr uint64_t kSessionTimeoutMs = 30000;

// COBS-delimited frames keep every byte value available to book contents.
std::size_t EncodeFrame(const Frame& frame, uint8_t* output, std::size_t capacity);
bool DecodeFrame(const uint8_t* input, std::size_t size, Frame* frame);

class Protocol final : public cli::CliBinarySession {
public:
    using Clock = uint64_t (*)();
    explicit Protocol(Channel& channel, Clock clock = nullptr);
    bool Start(cli::BoundedOutput* greeting) override;
    bool Poll(cli::CliTransport& transport) override;
    void Cancel() override;

private:
    bool Receive(cli::CliTransport& transport);
    bool Send(cli::CliTransport& transport);
    void Fail(cli::CliTransport& transport, Status status);
    Channel& channel_;
    Clock clock_;
    Frame frame_{};
    std::array<uint8_t, kWireSize> input_{}, output_{};
    std::array<uint8_t, 128> read_buffer_{};
    std::size_t size_ = 0, read_size_ = 0, read_offset_ = 0;
    uint32_t session_ = 0, last_id_ = 0;
    uint64_t last_frame_ms_ = 0;
    bool pending_ = false, overflow_ = false, failed_ = false, output_failed_ = false;
};

}  // namespace zectrix::host
