#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "zectrix_cli_core.h"

namespace zectrix::cli {

inline constexpr std::size_t kHistoryEntries = 8;

class CliTransport {
public:
    virtual ~CliTransport() = default;
    virtual bool IsConnected() const = 0;
    virtual std::size_t Read(uint8_t* destination, std::size_t capacity) = 0;
    virtual bool Write(const char* data, std::size_t size) = 0;
};

enum class ExecuteStatus : uint8_t {
    kOk,
    kUnknownCommand,
    kInvalidArguments,
    kUnavailable,
    kPending,
    kBusy,
    kTimeout,
};

class CliExecutor {
public:
    virtual ~CliExecutor() = default;
    virtual ExecuteStatus Execute(const Invocation& invocation,
                                  BoundedOutput* output) = 0;
    // Pending commands produce at most one bounded chunk per poll.
    virtual ExecuteStatus Poll(BoundedOutput*) { return ExecuteStatus::kOk; }
    virtual void Cancel() {}
};

class CliSession final {
public:
    CliSession(CliTransport& transport, CliExecutor& executor);

    // Poll is called only by the CLI owner task. It performs bounded work and
    // never waits for input or output itself.
    void Poll();
    void Reset();

    bool connected() const { return connected_; }
    std::size_t line_size() const { return line_size_; }
    std::size_t history_size() const { return history_size_; }
    bool command_active() const { return command_active_; }

private:
    enum class EscapeState : uint8_t { kNone, kEscape, kControlSequence };

    void OnConnected();
    void OnDisconnected();
    void ProcessByte(uint8_t value);
    void SubmitLine();
    void FinishExecution(ExecuteStatus status, const BoundedOutput& output);
    void AddHistory();
    void NavigateHistory(bool older);
    void RedrawLine();
    void ClearLine();
    bool Write(const char* text);
    bool Write(const char* data, std::size_t size);

    CliTransport& transport_;
    CliExecutor& executor_;
    std::array<char, kMaximumLineSize + 1> line_{};
    std::size_t line_size_ = 0;
    std::size_t cursor_ = 0;
    std::array<std::array<char, kMaximumLineSize + 1>, kHistoryEntries>
        history_{};
    std::size_t history_size_ = 0;
    std::size_t history_next_ = 0;
    std::size_t history_offset_ = 0;
    EscapeState escape_state_ = EscapeState::kNone;
    bool connected_ = false;
    bool previous_was_cr_ = false;
    bool command_active_ = false;
};

}  // namespace zectrix::cli
