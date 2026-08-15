#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace zectrix::cli {

inline constexpr std::size_t kMaximumLineSize = 256;
inline constexpr std::size_t kMaximumArguments = 12;
inline constexpr std::size_t kMaximumTokenSize = 64;
inline constexpr std::size_t kMaximumCommandDepth = 3;
inline constexpr std::size_t kMaximumOutputSize = 256;

enum class Access : uint8_t { kReadOnly, kConfirm, kLocalConfirm };
enum class Execution : uint8_t { kImmediate, kOwnerRequest, kStream };
enum class Origin : uint8_t { kUsbLocal, kAuthorizedCompanion };

enum class ParseStatus : uint8_t {
    kOk,
    kEmpty,
    kInvalidArgument,
    kLineTooLong,
    kTooManyArguments,
    kTokenTooLong,
    kUnterminatedQuote,
    kInvalidEscape,
};

struct Invocation {
    std::array<std::array<char, kMaximumTokenSize + 1>, kMaximumArguments>
        arguments{};
    std::size_t count = 0;

    const char* operator[](std::size_t index) const {
        return index < count ? arguments[index].data() : nullptr;
    }
};

struct CommandDescriptor {
    const char* name;
    const char* help;
    const char* usage;
    Access access;
    Execution execution;
    bool executable;
    const CommandDescriptor* children;
    std::size_t child_count;
};

enum class ResolveStatus : uint8_t {
    kOk,
    kInvalidArgument,
    kUnknownCommand,
    kIncompleteCommand,
    kDepthExceeded,
};

struct Resolution {
    const CommandDescriptor* command = nullptr;
    std::size_t argument_index = 0;
};

ParseStatus ParseLine(const char* line, std::size_t size,
                      Invocation* invocation);
ResolveStatus Resolve(const CommandDescriptor* roots, std::size_t root_count,
                      const Invocation& invocation, Resolution* resolution);

class BoundedOutput final {
public:
    bool Append(const char* text);
    bool Append(char value);
    void Clear();
    const char* data() const { return data_.data(); }
    std::size_t size() const { return size_; }
    bool truncated() const { return truncated_; }

private:
    std::array<char, kMaximumOutputSize + 1> data_{};
    std::size_t size_ = 0;
    bool truncated_ = false;
};

class CancellationToken final {
public:
    void Cancel() { cancelled_.store(true); }
    void Reset() { cancelled_.store(false); }
    bool IsCancelled() const { return cancelled_.load(); }

private:
    std::atomic<bool> cancelled_{false};
};

}  // namespace zectrix::cli
