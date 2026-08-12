#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace zectrix::app {

class ApplicationId {
public:
    static constexpr std::size_t kCapacity = 31;

    static bool Copy(const char* source, ApplicationId* output);

    bool empty() const { return value_[0] == '\0'; }
    const char* c_str() const { return value_.data(); }
    bool operator==(const ApplicationId& other) const;
    bool operator!=(const ApplicationId& other) const { return !(*this == other); }

private:
    std::array<char, kCapacity + 1> value_{};
};

enum class CommandKind : uint8_t {
    None,
    Open,
    Back,
    Home,
    Shutdown,
};

struct AppCommand {
    CommandKind kind = CommandKind::None;
    ApplicationId target{};

    static AppCommand Back();
    static AppCommand Home();
    static AppCommand Shutdown();
    static bool Open(const char* target, AppCommand* output);
};

enum class SubmitResult : uint8_t {
    Accepted,
    Superseded,
    Conflict,
};

class CommandArbiter {
public:
    SubmitResult Submit(const AppCommand& command);
    bool Take(AppCommand* output);
    bool has_pending() const { return pending_.kind != CommandKind::None; }

private:
    AppCommand pending_{};
};

struct DirtyRegion {
    int16_t x = 0;
    int16_t y = 0;
    int16_t width = 0;
    int16_t height = 0;

    bool IsEmpty() const { return width <= 0 || height <= 0; }
    static DirtyRegion Union(const DirtyRegion& first,
                             const DirtyRegion& second);
};

enum class RenderIntent : uint8_t {
    Fast,
    Quality,
};

struct RenderRequest {
    uint32_t owner_generation = 0;
    DirtyRegion dirty{};
    RenderIntent intent = RenderIntent::Fast;
};

class RenderAccumulator {
public:
    void Submit(const RenderRequest& request);
    bool TakeFor(uint32_t current_generation, RenderRequest* output);
    void Discard() { pending_ = false; }
    bool has_pending() const { return pending_; }

private:
    bool pending_ = false;
    RenderRequest request_{};
};

}  // namespace zectrix::app
