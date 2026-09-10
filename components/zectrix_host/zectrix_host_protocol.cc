#include "zectrix_host_protocol.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace zectrix::host {
namespace {
uint64_t Now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

std::size_t EncodeFrame(const Frame& frame, uint8_t* output, std::size_t capacity) {
    if (!output || frame.size > kPayloadSize || capacity < kWireSize) return 0;
    uint8_t header[kHeaderSize] = {'N', '4', 'U', '1', frame.operation,
        static_cast<uint8_t>(frame.status), static_cast<uint8_t>(frame.size),
        static_cast<uint8_t>(frame.size >> 8)};
    Write32(header + 8, frame.id);
    Write32(header + 12, frame.session);
    std::size_t code_at = 0, written = 1;
    uint8_t code = 1;
    for (std::size_t i = 0; i < kHeaderSize + frame.size; ++i) {
        const auto value = i < kHeaderSize ? header[i] : frame.payload[i - kHeaderSize];
        if (value == 0) {
            output[code_at] = code;
            code_at = written++;
            code = 1;
        } else {
            output[written++] = value;
            if (++code == 0xff) {
                output[code_at] = code;
                code_at = written++;
                code = 1;
            }
        }
    }
    output[code_at] = code;
    output[written++] = 0;
    return written;
}

bool DecodeFrame(const uint8_t* input, std::size_t size, Frame* frame) {
    if (!input || !frame || size == 0 || size >= kWireSize) return false;
    uint8_t header[kHeaderSize]{};
    std::size_t decoded = 0, cursor = 0;
    const auto append = [&](uint8_t value) {
        if (decoded >= kHeaderSize + kPayloadSize) return false;
        if (decoded < kHeaderSize) header[decoded] = value;
        else frame->payload[decoded - kHeaderSize] = value;
        ++decoded;
        return true;
    };
    while (cursor < size) {
        const auto code = input[cursor++];
        if (code == 0 || static_cast<std::size_t>(code - 1) > size - cursor) return false;
        for (unsigned i = 1; i < code; ++i) {
            if (input[cursor] == 0 || !append(input[cursor++])) return false;
        }
        if (code < 0xff && cursor < size && !append(0)) return false;
    }
    if (decoded < kHeaderSize || std::memcmp(header, "N4U1", 4) != 0) return false;
    frame->operation = header[4];
    frame->status = static_cast<Status>(header[5]);
    frame->size = header[6] | (uint16_t{header[7]} << 8);
    frame->id = Read32(header + 8);
    frame->session = Read32(header + 12);
    return frame->size <= kPayloadSize && decoded == kHeaderSize + frame->size;
}

Protocol::Protocol(Channel& channel, Clock clock) : channel_(channel), clock_(clock ? clock : Now) {}

bool Protocol::Start(cli::BoundedOutput* greeting) {
    if (!greeting || session_ != 0) return false;
    session_ = channel_.Connect();
    if (!session_) return false;
    size_ = read_size_ = read_offset_ = last_id_ = 0;
    pending_ = overflow_ = failed_ = output_failed_ = false;
    last_frame_ms_ = clock_();
    char text[64];
    std::snprintf(text, sizeof(text), "N4USB 1 %lu %zu", static_cast<unsigned long>(session_), kPayloadSize);
    greeting->Append(text);
    return true;
}

void Protocol::Cancel() {
    channel_.Disconnect(session_);
    session_ = 0;
    pending_ = false;
    size_ = read_size_ = read_offset_ = 0;
}

bool Protocol::Send(cli::CliTransport& transport) {
    const auto size = EncodeFrame(frame_, output_.data(), output_.size());
    if (!output_failed_ && size && transport.Write(reinterpret_cast<const char*>(output_.data()), size)) return true;
    // Never reinterpret binary input as commands after a failed write.
    output_failed_ = failed_ = true;
    pending_ = false;
    channel_.Disconnect(session_);
    return false;
}

void Protocol::Fail(cli::CliTransport& transport, Status status) {
    channel_.Disconnect(session_);
    pending_ = false;
    if (failed_) return;
    failed_ = true;
    frame_.operation = 0x80;
    frame_.status = status;
    frame_.id = 0;
    frame_.session = session_;
    frame_.size = 0;
    Send(transport);
}

bool Protocol::Receive(cli::CliTransport& transport) {
    if (overflow_ || !DecodeFrame(input_.data(), size_, &frame_) ||
        frame_.session != session_ || frame_.status != Status::Ok ||
        frame_.id == 0 || frame_.id <= last_id_ || frame_.operation == 0 ||
        frame_.operation > static_cast<uint8_t>(Operation::Close)) {
        Fail(transport, Status::Invalid);
        return true;
    }
    last_id_ = frame_.id;
    last_frame_ms_ = clock_();
    if (frame_.operation == static_cast<uint8_t>(Operation::Close) && frame_.size == 0) {
        channel_.Disconnect(session_);
        frame_.operation |= 0x80;
        frame_.status = Status::Ok;
        return !Send(transport);
    }
    const auto status = failed_ ? Status::Cancelled : channel_.Submit(frame_);
    if (status == Status::Ok) pending_ = true;
    else {
        frame_.operation |= 0x80;
        frame_.status = status;
        frame_.size = 0;
        Send(transport);
    }
    return true;
}

bool Protocol::Poll(cli::CliTransport& transport) {
    if (session_ == 0) return true;
    if (!failed_ && clock_() - last_frame_ms_ >= kSessionTimeoutMs) Fail(transport, Status::Timeout);
    if (pending_) {
        if (channel_.TakeReply(session_, &frame_)) {
            pending_ = false;
            Send(transport);
        } else if (channel_.Session() != session_) Fail(transport, Status::Cancelled);
        else return true;
    }
    // Stop-and-wait bounds both queues; keep unread tail bytes in owned storage.
    for (std::size_t budget = 0; budget < 2048; ++budget) {
        if (read_offset_ == read_size_) {
            read_size_ = std::min(transport.Read(read_buffer_.data(), read_buffer_.size()), read_buffer_.size());
            read_offset_ = 0;
            if (!read_size_) break;
        }
        const auto value = read_buffer_[read_offset_++];
        if (value == 0) {
            if (size_ == 0 && !overflow_) continue;
            const bool active = Receive(transport);
            size_ = 0;
            overflow_ = false;
            return active;
        }
        if (size_ < input_.size() - 1) input_[size_++] = value;
        else overflow_ = true;
    }
    return true;
}

}  // namespace zectrix::host
