#include "zectrix_cli_session.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace zectrix::cli {
namespace {

constexpr char kPrompt[] = "zectrix> ";

const char* ParseError(ParseStatus status) {
    switch (status) {
        case ParseStatus::kLineTooLong: return "line too long";
        case ParseStatus::kTooManyArguments: return "too many arguments";
        case ParseStatus::kTokenTooLong: return "token too long";
        case ParseStatus::kUnterminatedQuote: return "unterminated quote";
        case ParseStatus::kInvalidEscape: return "invalid escape";
        default: return "invalid input";
    }
}

const char* ExecuteError(ExecuteStatus status) {
    switch (status) {
        case ExecuteStatus::kUnknownCommand: return "unknown command";
        case ExecuteStatus::kInvalidArguments: return "invalid arguments";
        case ExecuteStatus::kUnavailable: return "temporarily unavailable";
        case ExecuteStatus::kOk: break;
    }
    return "command failed";
}

}  // namespace

CliSession::CliSession(CliTransport& transport, CliExecutor& executor)
    : transport_(transport), executor_(executor) {}

void CliSession::Poll() {
    const bool now_connected = transport_.IsConnected();
    if (!now_connected) {
        if (connected_) OnDisconnected();
        return;
    }
    if (!connected_) OnConnected();

    std::array<uint8_t, 32> input{};
    const std::size_t received =
        std::min(transport_.Read(input.data(), input.size()), input.size());
    for (std::size_t index = 0; index < received && connected_; ++index) {
        ProcessByte(input[index]);
    }
}

void CliSession::Reset() {
    connected_ = false;
    previous_was_cr_ = false;
    escape_state_ = EscapeState::kNone;
    history_offset_ = 0;
    ClearLine();
}

void CliSession::OnConnected() {
    connected_ = true;
    previous_was_cr_ = false;
    escape_state_ = EscapeState::kNone;
    history_offset_ = 0;
    ClearLine();
    Write("\r\nZectrix maintenance CLI\r\n");
    Write(kPrompt);
}

void CliSession::OnDisconnected() { Reset(); }

void CliSession::ProcessByte(uint8_t value) {
    if (escape_state_ == EscapeState::kEscape) {
        escape_state_ =
            value == '[' ? EscapeState::kControlSequence : EscapeState::kNone;
        return;
    }
    if (escape_state_ == EscapeState::kControlSequence) {
        escape_state_ = EscapeState::kNone;
        if (value == 'A') NavigateHistory(true);
        if (value == 'B') NavigateHistory(false);
        if (value == 'C' && cursor_ < line_size_) {
            ++cursor_;
            Write("\x1b[C");
        }
        if (value == 'D' && cursor_ > 0) {
            --cursor_;
            Write("\x1b[D");
        }
        return;
    }
    if (value == 0x1b) {
        escape_state_ = EscapeState::kEscape;
        return;
    }
    if (value == 0x03) {
        ClearLine();
        history_offset_ = 0;
        Write("^C\r\n");
        Write(kPrompt);
        return;
    }
    if (value == '\r' || value == '\n') {
        if (value == '\n' && previous_was_cr_) {
            previous_was_cr_ = false;
            return;
        }
        previous_was_cr_ = value == '\r';
        SubmitLine();
        return;
    }
    previous_was_cr_ = false;
    if (value == 0x08 || value == 0x7f) {
        if (cursor_ == 0) return;
        std::memmove(line_.data() + cursor_ - 1, line_.data() + cursor_,
                     line_size_ - cursor_ + 1);
        --cursor_;
        --line_size_;
        RedrawLine();
        return;
    }
    if (value < 0x20 || value > 0x7e) return;
    if (line_size_ == kMaximumLineSize) {
        Write("\a");
        return;
    }
    std::memmove(line_.data() + cursor_ + 1, line_.data() + cursor_,
                 line_size_ - cursor_ + 1);
    line_[cursor_++] = static_cast<char>(value);
    ++line_size_;
    history_offset_ = 0;
    RedrawLine();
}

void CliSession::SubmitLine() {
    Write("\r\n");
    Invocation invocation{};
    const ParseStatus parse = ParseLine(line_.data(), line_size_, &invocation);
    if (parse == ParseStatus::kOk) {
        AddHistory();
        BoundedOutput output;
        const ExecuteStatus execute = executor_.Execute(invocation, &output);
        if (execute == ExecuteStatus::kOk) {
            if (output.size() != 0) {
                Write(output.data(), output.size());
                Write("\r\n");
            }
        } else {
            Write("error: ");
            Write(ExecuteError(execute));
            Write("\r\n");
        }
    } else if (parse != ParseStatus::kEmpty) {
        Write("error: ");
        Write(ParseError(parse));
        Write("\r\n");
    }
    ClearLine();
    history_offset_ = 0;
    Write(kPrompt);
}

void CliSession::AddHistory() {
    if (line_size_ == 0) return;
    if (history_size_ != 0) {
        const std::size_t latest =
            (history_next_ + kHistoryEntries - 1) % kHistoryEntries;
        if (std::strcmp(history_[latest].data(), line_.data()) == 0) return;
    }
    std::memcpy(history_[history_next_].data(), line_.data(), line_size_ + 1);
    history_next_ = (history_next_ + 1) % kHistoryEntries;
    if (history_size_ < kHistoryEntries) ++history_size_;
}

void CliSession::NavigateHistory(bool older) {
    if (history_size_ == 0) return;
    if (older) {
        if (history_offset_ < history_size_) ++history_offset_;
    } else if (history_offset_ > 0) {
        --history_offset_;
    }
    ClearLine();
    if (history_offset_ != 0) {
        const std::size_t index =
            (history_next_ + kHistoryEntries - history_offset_) %
            kHistoryEntries;
        line_size_ = std::strlen(history_[index].data());
        std::memcpy(line_.data(), history_[index].data(), line_size_ + 1);
        cursor_ = line_size_;
    }
    RedrawLine();
}

void CliSession::RedrawLine() {
    Write("\r\x1b[2K");
    Write(kPrompt);
    Write(line_.data(), line_size_);
    if (cursor_ < line_size_) {
        char movement[16]{};
        const int written = std::snprintf(movement, sizeof(movement), "\x1b[%zuD",
                                          line_size_ - cursor_);
        if (written > 0 && static_cast<std::size_t>(written) < sizeof(movement)) {
            Write(movement, static_cast<std::size_t>(written));
        }
    }
}

void CliSession::ClearLine() {
    line_ = {};
    line_size_ = 0;
    cursor_ = 0;
}

bool CliSession::Write(const char* text) {
    return text != nullptr && Write(text, std::strlen(text));
}

bool CliSession::Write(const char* data, std::size_t size) {
    if (!connected_ || data == nullptr || size == 0) return size == 0;
    if (!transport_.Write(data, size)) {
        OnDisconnected();
        return false;
    }
    return true;
}

}  // namespace zectrix::cli
