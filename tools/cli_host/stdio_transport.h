#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <termios.h>

#include "zectrix_cli_session.h"

namespace zectrix::cli::host {

template <std::size_t Capacity>
class ByteQueue final {
public:
    std::size_t size() const { return size_; }
    std::size_t space() const { return Capacity - size_; }
    uint8_t front() const { return data_[head_]; }
    void Push(uint8_t value) { data_[(head_ + size_++) % Capacity] = value; }
    uint8_t Pop() {
        const auto value = front();
        head_ = (head_ + 1) % Capacity;
        --size_;
        return value;
    }
    const uint8_t* data() const { return data_.data() + head_; }
    std::size_t contiguous_size() const {
        return size_ < Capacity - head_ ? size_ : Capacity - head_;
    }
    void Clear() { head_ = size_ = 0; }

private:
    std::array<uint8_t, Capacity> data_{};
    std::size_t head_ = 0;
    std::size_t size_ = 0;
};

class StdioTransport final : public CliTransport {
public:
    StdioTransport() = default;
    ~StdioTransport();
    StdioTransport(const StdioTransport&) = delete;
    StdioTransport& operator=(const StdioTransport&) = delete;

    bool Open();
    void Pump(int timeout_ms, bool read_input = true);
    void DrainOutput(int timeout_ms);
    void Reconnect();
    void SetCommandActive(bool active) { command_active_ = active; }
    void Inject(uint8_t value) { injected_ = value; }

    bool IsConnected() const override { return connected_; }
    std::size_t Read(uint8_t* destination, std::size_t capacity) override;
    bool Write(const char* data, std::size_t size) override;

    bool eof() const { return eof_; }
    bool input_closing() const { return eof_ || hangup_ || regular_input_; }
    bool input_empty() const { return input_.size() == 0 && injected_ < 0; }
    bool quit_requested() const { return quit_; }
    bool reconnect_requested() const { return reconnect_; }
    int error() const { return error_; }

private:
    void Fail(int error);

    ByteQueue<512> input_;
    ByteQueue<2048> output_;
    termios saved_terminal_{};
    int input_flags_ = -1;
    int output_flags_ = -1;
    int injected_ = -1;
    int error_ = 0;
    bool terminal_changed_ = false;
    bool interactive_ = false;
    bool regular_input_ = false;
    bool connected_ = false;
    bool command_active_ = false;
    bool eof_ = false;
    bool hangup_ = false;
    bool quit_ = false;
    bool reconnect_ = false;
};

}  // namespace zectrix::cli::host
