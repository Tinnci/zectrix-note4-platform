#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace zectrix::host {

inline constexpr std::size_t kPayloadSize = 1024;
inline constexpr std::size_t kChunkSize = kPayloadSize - 4;
enum class Operation : uint8_t {
    Info = 1, List, ReadOpen, Read, UploadBegin, UploadChunk, UploadCommit,
    Abort, GetSetting, SetSetting, Close,
    AppList, AppReadOpen, AppUploadBegin, AppRemove,
};
enum class Status : uint8_t {
    Ok, Invalid, Busy, Unavailable, Exists, NotFound, NoSpace, IoError,
    Cancelled, Timeout, NotSaved,
};
enum class Setting : uint8_t { Language, AutoShowcase, SleepCover };

struct Frame {
    uint8_t operation = 0;
    Status status = Status::Ok;
    uint16_t size = 0;
    uint32_t id = 0, session = 0;
    std::array<uint8_t, kPayloadSize> payload{};
};

inline uint32_t Read32(const uint8_t* data) {
    return uint32_t{data[0]} | (uint32_t{data[1]} << 8) |
           (uint32_t{data[2]} << 16) | (uint32_t{data[3]} << 24);
}
inline void Write32(uint8_t* data, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) data[i] = static_cast<uint8_t>(value >> (i * 8));
}

// One copied request/reply crosses from the USB worker to the foreground.
// Executing work retains its slot after cancellation until the owner returns.
class Channel final {
public:
    void Enable();
    void Disable();
    uint32_t Connect();
    void Disconnect(uint32_t session);
    uint32_t Session() const;
    Status Submit(const Frame& request);
    bool TakeRequest(Frame* request);
    void Complete(const Frame& reply);
    bool TakeReply(uint32_t session, Frame* reply);

private:
    enum class State : uint8_t { Idle, Queued, Executing, Replied };
    void Retire();
    mutable std::mutex mutex_;
    Frame slot_{};
    State state_ = State::Idle;
    uint32_t session_ = 0, next_session_ = 0;
    bool enabled_ = false;
};

}  // namespace zectrix::host
