#include "stdio_transport.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

namespace zectrix::cli::host {

StdioTransport::~StdioTransport() {
    if (terminal_changed_) tcsetattr(STDIN_FILENO, TCSANOW, &saved_terminal_);
    if (output_flags_ >= 0) fcntl(STDOUT_FILENO, F_SETFL, output_flags_);
    if (input_flags_ >= 0) fcntl(STDIN_FILENO, F_SETFL, input_flags_);
}

bool StdioTransport::Open() {
    // Save both flag sets before changing either: stdin and stdout can share
    // an open-file description when launched through a pseudo-terminal.
    input_flags_ = fcntl(STDIN_FILENO, F_GETFL);
    output_flags_ = fcntl(STDOUT_FILENO, F_GETFL);
    if (input_flags_ < 0 || output_flags_ < 0) {
        Fail(errno);
        return false;
    }
    interactive_ = isatty(STDIN_FILENO);
    struct stat input_status{};
    if (fstat(STDIN_FILENO, &input_status) < 0) {
        Fail(errno);
        return false;
    }
    // File scripts are finite even when queued commands prevent reading EOF.
    regular_input_ = S_ISREG(input_status.st_mode);
    if (interactive_) {
        if (tcgetattr(STDIN_FILENO, &saved_terminal_) < 0) {
            Fail(errno);
            return false;
        }
        termios raw = saved_terminal_;
        cfmakeraw(&raw);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0) {
            Fail(errno);
            return false;
        }
        terminal_changed_ = true;
    }
    if (fcntl(STDIN_FILENO, F_SETFL, input_flags_ | O_NONBLOCK) < 0 ||
        fcntl(STDOUT_FILENO, F_SETFL, output_flags_ | O_NONBLOCK) < 0) {
        Fail(errno);
        return false;
    }
    connected_ = true;
    return true;
}

void StdioTransport::Fail(int error) {
    error_ = error;
    connected_ = false;
}

void StdioTransport::Pump(int timeout_ms, bool read_input) {
    if (!connected_) return;
    pollfd descriptors[] = {
        {read_input && !eof_ && (input_.space() != 0 || !hangup_) ? STDIN_FILENO : -1,
         static_cast<short>(input_.space() != 0 ? POLLIN : 0), 0},
        {output_.size() != 0 ? STDOUT_FILENO : -1, POLLOUT, 0},
    };
    const int ready = poll(descriptors, 2, timeout_ms);
    if (ready < 0) {
        if (errno != EINTR) Fail(errno);
        return;
    }
    if (descriptors[0].revents & POLLHUP) hangup_ = true;
    if ((descriptors[0].revents & (POLLIN | POLLHUP)) && input_.space() != 0) {
        uint8_t bytes[128];
        const auto received = read(STDIN_FILENO, bytes,
                                    std::min(input_.space(), sizeof(bytes)));
        if (received > 0) {
            for (ssize_t index = 0; index < received; ++index) input_.Push(bytes[index]);
        } else if (received == 0 || (received < 0 && errno == EIO && interactive_)) {
            eof_ = true;
            if (interactive_) connected_ = false;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            Fail(errno);
        }
    }
    if (descriptors[0].revents & POLLNVAL) Fail(EBADF);
    if (descriptors[0].revents & POLLERR) {
        if (interactive_) connected_ = false;
        else Fail(EIO);
    }
    if (descriptors[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
        Fail(EPIPE);
    } else if (descriptors[1].revents & POLLOUT) {
        const auto written = write(STDOUT_FILENO, output_.data(), output_.contiguous_size());
        if (written > 0) {
            for (ssize_t index = 0; index < written; ++index) output_.Pop();
        } else if (written < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            Fail(errno);
        }
    }
}

std::size_t StdioTransport::Read(uint8_t* destination, std::size_t capacity) {
    if (destination == nullptr || capacity == 0 || !connected_) return 0;
    if (injected_ >= 0) {
        destination[0] = static_cast<uint8_t>(injected_);
        injected_ = -1;
        return 1;
    }
    std::size_t count = 0;
    while (count < capacity && input_.size() != 0) {
        const auto value = input_.front();
        // Pipes submit one line at a time. Leave the next command queued while
        // the real executor pages a reply; cancellation remains available.
        if (!interactive_ && command_active_ && value != 0x03 &&
            value != 0x04 && value != 0x12) break;
        input_.Pop();
        if (value == 0x04 || value == 0x12) {
            quit_ = value == 0x04;
            reconnect_ = value == 0x12;
            break;
        }
        destination[count++] = value;
        if (!interactive_ && (value == '\r' || value == '\n' || value == 0x03)) break;
    }
    return count;
}

bool StdioTransport::Write(const char* data, std::size_t size) {
    if (!connected_ || data == nullptr) return false;
    if (size > output_.space()) {
        // Match the USB driver's bounded backpressure recovery: cancel the
        // session and recover its prompt instead of blocking the producer.
        output_.Clear();
        return false;
    }
    for (std::size_t index = 0; index < size; ++index) output_.Push(data[index]);
    return true;
}

void StdioTransport::DiscardInput() {
    input_.Clear();
    injected_ = -1;
}

void StdioTransport::Reconnect() {
    DiscardInput();
    output_.Clear();
    reconnect_ = false;
}

void StdioTransport::DrainOutput(int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (connected_ && output_.size() != 0 && std::chrono::steady_clock::now() < deadline) {
        Pump(10, false);
    }
}

}  // namespace zectrix::cli::host
