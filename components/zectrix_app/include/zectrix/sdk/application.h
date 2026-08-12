#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "zectrix/sdk/input.h"
#include "zectrix/sdk/status.h"

namespace zectrix::sdk {
inline namespace v1 {

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

enum class CommandKind : std::uint8_t { None, Open, Back, Home, Shutdown };

struct AppCommand {
    CommandKind kind = CommandKind::None;
    ApplicationId target{};

    static AppCommand Back();
    static AppCommand Home();
    static AppCommand Shutdown();
    static bool Open(const char* target, AppCommand* output);
};

enum class SubmitResult : std::uint8_t {
    Accepted,
    Superseded,
    Conflict,
    Rejected,
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
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::int16_t width = 0;
    std::int16_t height = 0;

    bool IsEmpty() const { return width <= 0 || height <= 0; }
    static DirtyRegion Union(const DirtyRegion& first,
                             const DirtyRegion& second);
};

enum class RenderIntent : std::uint8_t { Fast, Quality };

struct RenderRequest {
    std::uint32_t owner_generation = 0;
    DirtyRegion dirty{};
    RenderIntent intent = RenderIntent::Fast;
};

class RenderAccumulator {
public:
    void Submit(const RenderRequest& request);
    bool TakeFor(std::uint32_t current_generation, RenderRequest* output);
    void Discard() { pending_ = false; }
    bool has_pending() const { return pending_; }

private:
    bool pending_ = false;
    RenderRequest request_{};
};

class ApplicationContext;
class ApplicationRegistry;

class Application {
public:
    virtual ~Application() = default;
    virtual Status Enter(ApplicationContext& context) = 0;
    virtual Status HandleEvent(const InputEvent& event,
                               ApplicationContext& context) = 0;
    virtual Status HandleIdle(ApplicationContext&) { return Status::Ok; }
    virtual Status Render(const RenderRequest& request) = 0;
    virtual Status Exit() = 0;
};

// The composition root owns each factory. The factory must remain valid for
// the registry and runtime lifetime. A successful call transfers one inactive
// Application to the runtime through output.
class ApplicationFactory {
public:
    virtual ~ApplicationFactory() = default;
    virtual Status Create(const ApplicationRegistry& registry,
                          Application** output) = 0;
};

struct ApplicationDescriptor {
    const char* id = nullptr;
    const char* display_name = nullptr;
    ApplicationFactory* factory = nullptr;
};

class ApplicationRegistry {
public:
    ApplicationRegistry(const ApplicationDescriptor* descriptors,
                        std::size_t descriptor_count)
        : descriptors_(descriptors), descriptor_count_(descriptor_count) {}

    Status Validate(const ApplicationId& launcher_id) const;
    const ApplicationDescriptor* Find(const ApplicationId& id) const;
    const ApplicationDescriptor* At(std::size_t index) const;
    std::size_t size() const { return descriptor_count_; }

private:
    const ApplicationDescriptor* descriptors_;
    std::size_t descriptor_count_;
};

enum class LifecycleState : std::uint8_t {
    Absent,
    Creating,
    Entering,
    Active,
    Exiting,
    Failsafe,
    Stopped,
};

class RuntimeDelegate {
public:
    virtual ~RuntimeDelegate() = default;
    virtual Status Shutdown() = 0;
    virtual void EnterFailsafe(Status reason) = 0;
};

class ApplicationRuntime;

class ApplicationContext {
public:
    SubmitResult RequestCommand(const AppCommand& command);
    bool RequestRender(const DirtyRegion& dirty, RenderIntent intent);
    std::uint32_t foreground_generation() const;

private:
    friend class ApplicationRuntime;
    explicit ApplicationContext(ApplicationRuntime& runtime)
        : runtime_(&runtime) {}
    ApplicationRuntime* runtime_;
};

// ApplicationRuntime serializes all callbacks on its caller's execution
// context. SDK v1 does not create a task and does not provide concurrent calls.
class ApplicationRuntime {
public:
    ApplicationRuntime(const ApplicationDescriptor* descriptors,
                       std::size_t descriptor_count, const char* launcher_id,
                       RuntimeDelegate& delegate);
    ~ApplicationRuntime();

    ApplicationRuntime(const ApplicationRuntime&) = delete;
    ApplicationRuntime& operator=(const ApplicationRuntime&) = delete;

    Status Start();
    Status Step(const InputEvent* event = nullptr);
    Status Idle();
    Status Stop();

    LifecycleState state() const { return state_; }
    const ApplicationId& foreground_id() const { return foreground_id_; }
    std::uint32_t foreground_generation() const { return generation_; }
    Status last_error() const { return last_error_; }
    bool HasForeground() const { return foreground_ != nullptr; }
    const ApplicationRegistry& registry() const { return registry_; }

private:
    friend class ApplicationContext;

    Status SwitchTo(const ApplicationId& id, bool allow_fallback);
    Status EnterLauncherFallback(Status reason);
    void EnterFailsafe(Status reason);
    void ExitAndDestroyForeground();
    Status ProcessCommand();
    Status ProcessRender();
    SubmitResult SubmitCommand(const AppCommand& command);
    bool SubmitRender(const DirtyRegion& dirty, RenderIntent intent);

    template <typename Callback>
    Status InvokeCallback(Callback callback) {
        callback_active_ = true;
        const Status result = callback();
        callback_active_ = false;
        return result;
    }

    ApplicationRegistry registry_;
    ApplicationId launcher_id_{};
    RuntimeDelegate* delegate_;
    std::unique_ptr<Application> foreground_;
    ApplicationId foreground_id_{};
    ApplicationContext context_;
    CommandArbiter commands_;
    RenderAccumulator renders_;
    LifecycleState state_ = LifecycleState::Absent;
    std::uint32_t generation_ = 0;
    Status last_error_ = Status::Ok;
    bool callback_active_ = false;
};

}  // namespace v1
}  // namespace zectrix::sdk
