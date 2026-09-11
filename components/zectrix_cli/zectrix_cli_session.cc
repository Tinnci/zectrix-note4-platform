#include "zectrix_cli_session.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace zectrix::cli {
namespace {

constexpr char kPrompt[] = "zectrix> ";
constexpr std::size_t kMaximumEscapeSize = 16;

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
        case ExecuteStatus::kBusy: return "command busy";
        case ExecuteStatus::kTimeout: return "owner request timed out";
        case ExecuteStatus::kDenied: return "confirmation denied or expired";
        case ExecuteStatus::kUnknownOutcome: return "outcome unknown; inspect device before retrying";
        case ExecuteStatus::kPending: break;
        case ExecuteStatus::kBinary: break;
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
    if (!connected_) return;

    if (binary_ != nullptr) {
        if (!binary_->Poll(transport_)) {
            binary_->Cancel();
            binary_ = nullptr;
            transport_.DiscardInput();
            ClearLine();
            previous_was_cr_ = false;
            Write("\r\n");
            Write(kPrompt);
        }
        return;
    }

    std::array<uint8_t, 32> input{};
    const std::size_t received =
        std::min(transport_.Read(input.data(), input.size()), input.size());
    for (std::size_t index = 0; index < received && connected_; ++index) {
        ProcessByte(input[index]);
        // The peer must wait for the greeting before sending binary bytes.
        if (binary_ != nullptr) {
            transport_.DiscardInput();
            break;
        }
    }
    if (connected_ && command_active_) {
        BoundedOutput output;
        const ExecuteStatus status = executor_.Poll(&output);
        FinishExecution(status, output);
    }
}

void CliSession::Reset() {
    if (binary_ != nullptr) binary_->Cancel();
    binary_ = nullptr;
    executor_.Cancel();
    transport_.DiscardInput();
    command_active_ = false;
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
    // Cancellation takes precedence even in a partial ANSI escape sequence.
    if (value == 0x03) {
        const auto cancelled = executor_.CancelStatus();
        command_active_ = false;
        escape_state_ = EscapeState::kNone;
        previous_was_cr_ = false;
        ClearLine();
        history_offset_ = 0;
        Write("^C\r\n");
        if (cancelled == ExecuteStatus::kUnknownOutcome) {
            Write(ExecuteError(cancelled));
            Write("\r\n");
        }
        Write(kPrompt);
        return;
    }
    if (command_active_) return;
    if (value == '\r' || value == '\n') {
        if (value == '\n' && previous_was_cr_) {
            previous_was_cr_ = false;
            return;
        }
        previous_was_cr_ = value == '\r';
        if (escape_state_ != EscapeState::kNone) {
            RejectLine(ParseStatus::kInvalidEscape);
        }
        SubmitLine();
        return;
    }
    previous_was_cr_ = false;
    if (line_error_ != ParseStatus::kOk) return;
    if (escape_state_ != EscapeState::kNone) {
        ProcessEscape(value);
        return;
    }
    if (value == 0x1b) {
        escape_state_ = EscapeState::kEscape;
        escape_size_ = escape_parameter_ = 0;
        escape_parameter_valid_ = true;
        return;
    }
    if (value == 0x01 || value == 0x05) {
        MoveCursor(value == 0x01 ? 0 : line_size_);
        return;
    }
    if (value == 0x08 || value == 0x7f) {
        if (cursor_ == 0) return;
        std::memmove(line_.data() + cursor_ - 1, line_.data() + cursor_,
                     line_size_ - cursor_ + 1);
        --cursor_;
        --line_size_;
        RedrawLine();
        return;
    }
    if (value == '\t') value = ' ';
    if (value < 0x20 || value > 0x7e) {
        RejectLine(ParseStatus::kInvalidArgument);
        return;
    }
    if (line_size_ == kMaximumLineSize) {
        RejectLine(ParseStatus::kLineTooLong);
        return;
    }
    const bool append = cursor_ == line_size_;
    std::memmove(line_.data() + cursor_ + 1, line_.data() + cursor_,
                 line_size_ - cursor_ + 1);
    line_[cursor_++] = static_cast<char>(value);
    ++line_size_;
    history_offset_ = 0;
    if (append) Write(line_.data() + cursor_ - 1, 1);
    else RedrawLine();
}

void CliSession::ProcessEscape(uint8_t value) {
    if (escape_state_ == EscapeState::kEscape) {
        if (value == '[') escape_state_ = EscapeState::kControlSequence;
        else if (value == 'O') escape_state_ = EscapeState::kSs3;
        else RejectLine(ParseStatus::kInvalidEscape);
        return;
    }
    if (++escape_size_ > kMaximumEscapeSize || value < 0x20 || value > 0x7e) {
        RejectLine(ParseStatus::kInvalidEscape);
        return;
    }
    if (value >= 0x20 && value <= 0x3f) {
        if (escape_state_ == EscapeState::kControlSequence &&
            escape_parameter_valid_ && value >= '0' && value <= '9') {
            escape_parameter_ = std::min(kMaximumLineSize,
                escape_parameter_ * 10 + static_cast<std::size_t>(value - '0'));
        } else {
            escape_parameter_valid_ = false;
        }
        return;
    }
    // Consume the complete CSI/SS3 sequence, including unsupported parameters.
    escape_state_ = EscapeState::kNone;
    if (!escape_parameter_valid_) return;
    const auto count = std::max<std::size_t>(1, escape_parameter_);
    switch (value) {
        case 'A': if (count == 1) NavigateHistory(true); break;
        case 'B': if (count == 1) NavigateHistory(false); break;
        case 'C': MoveCursor(cursor_ + std::min(count, line_size_ - cursor_)); break;
        case 'D': MoveCursor(cursor_ - std::min(count, cursor_)); break;
        case 'H': if (count == 1) MoveCursor(0); break;
        case 'F': if (count == 1) MoveCursor(line_size_); break;
        case '~':
            if (escape_parameter_ == 1 || escape_parameter_ == 7) MoveCursor(0);
            else if (escape_parameter_ == 4 || escape_parameter_ == 8) MoveCursor(line_size_);
            else if (escape_parameter_ == 3 && cursor_ < line_size_) {
                std::memmove(line_.data() + cursor_, line_.data() + cursor_ + 1,
                             line_size_ - cursor_);
                --line_size_;
                RedrawLine();
            }
            break;
        default: break;
    }
}

void CliSession::RejectLine(ParseStatus error) {
    escape_state_ = EscapeState::kNone;
    if (line_error_ != ParseStatus::kOk) return;
    line_error_ = error;
    // One bell per rejected line keeps pasted input from exhausting USB TX.
    Write("\a");
}

void CliSession::MoveCursor(std::size_t position) {
    if (position == cursor_) return;
    const auto distance = position > cursor_ ? position - cursor_ : cursor_ - position;
    char movement[16]{};
    const int size = std::snprintf(movement, sizeof(movement), "\x1b[%zu%c",
                                   distance, position > cursor_ ? 'C' : 'D');
    cursor_ = position;
    if (size > 0 && static_cast<std::size_t>(size) < sizeof(movement)) {
        Write(movement, static_cast<std::size_t>(size));
    }
}

void CliSession::SubmitLine() {
    if (!Write("\r\n")) return;
    Invocation invocation{};
    const ParseStatus parse = line_error_ == ParseStatus::kOk
        ? ParseLine(line_.data(), line_size_, &invocation) : line_error_;
    if (parse == ParseStatus::kOk) {
        AddHistory();
        BoundedOutput output;
        const ExecuteStatus execute = executor_.Execute(invocation, &output);
        ClearLine();
        history_offset_ = 0;
        FinishExecution(execute, output);
        return;
    } else if (parse != ParseStatus::kEmpty) {
        executor_.Cancel();
        Write("error: ");
        Write(ParseError(parse));
        Write("\r\n");
    }
    ClearLine();
    history_offset_ = 0;
    Write(kPrompt);
}

void CliSession::FinishExecution(ExecuteStatus status,
                                 const BoundedOutput& output) {
    if (status == ExecuteStatus::kBinary) binary_ = executor_.BinarySession();
    command_active_ = status == ExecuteStatus::kPending;
    if (output.size() != 0) {
        if (!Write(output.data(), output.size()) || !Write("\r\n")) return;
    }
    if (status != ExecuteStatus::kOk && status != ExecuteStatus::kPending &&
        status != ExecuteStatus::kBinary) {
        executor_.Cancel();
        Write("error: ");
        Write(ExecuteError(status));
        Write("\r\n");
    }
    if (!command_active_ && binary_ == nullptr) Write(kPrompt);
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
    line_error_ = ParseStatus::kOk;
    escape_state_ = EscapeState::kNone;
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
