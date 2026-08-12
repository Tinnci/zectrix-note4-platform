#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "esp_err.h"
#include "zectrix_app_contract.h"
#include "zectrix_input_service.h"

namespace zectrix { class Platform; }

namespace zectrix::app {

class ApplicationContext;
class ApplicationRegistry;
class ApplicationFactoryContext {
public:
    virtual ~ApplicationFactoryContext() = default;
};

class Application {
public:
    virtual ~Application() = default;
    virtual esp_err_t Enter(ApplicationContext& context) = 0;
    virtual esp_err_t HandleEvent(const input::InputEvent& event,
                                  ApplicationContext& context) = 0;
    virtual esp_err_t HandleIdle(ApplicationContext&) { return ESP_OK; }
    virtual esp_err_t Render(const RenderRequest& request) = 0;
    virtual esp_err_t Exit() = 0;
};

using ApplicationFactory = esp_err_t (*)(Platform& platform,
                                         const ApplicationRegistry& registry,
                                         ApplicationFactoryContext* factory_context,
                                         Application** output);

struct ApplicationDescriptor {
    const char* id = nullptr;
    const char* display_name = nullptr;
    ApplicationFactory factory = nullptr;
    ApplicationFactoryContext* factory_context = nullptr;
};

class ApplicationRegistry {
public:
    ApplicationRegistry(const ApplicationDescriptor* descriptors,
                        std::size_t descriptor_count)
        : descriptors_(descriptors), descriptor_count_(descriptor_count) {}

    esp_err_t Validate(const ApplicationId& launcher_id) const;
    const ApplicationDescriptor* Find(const ApplicationId& id) const;
    const ApplicationDescriptor* At(std::size_t index) const;
    std::size_t size() const { return descriptor_count_; }

private:
    const ApplicationDescriptor* descriptors_;
    std::size_t descriptor_count_;
};

enum class LifecycleState : uint8_t {
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
    virtual esp_err_t Shutdown() = 0;
    virtual void EnterFailsafe(esp_err_t reason) = 0;
};

class ApplicationRuntime;

class ApplicationContext {
public:
    SubmitResult RequestCommand(const AppCommand& command);
    bool RequestRender(const DirtyRegion& dirty, RenderIntent intent);
    uint32_t foreground_generation() const;

private:
    friend class ApplicationRuntime;
    explicit ApplicationContext(ApplicationRuntime& runtime)
        : runtime_(&runtime) {}
    ApplicationRuntime* runtime_;
};

class ApplicationRuntime {
public:
    ApplicationRuntime(const ApplicationDescriptor* descriptors,
                       std::size_t descriptor_count, const char* launcher_id,
                       Platform& platform, RuntimeDelegate& delegate);
    ~ApplicationRuntime();

    ApplicationRuntime(const ApplicationRuntime&) = delete;
    ApplicationRuntime& operator=(const ApplicationRuntime&) = delete;

    esp_err_t Start();
    esp_err_t Step(const input::InputEvent* event = nullptr);
    esp_err_t Idle();
    esp_err_t Stop();

    LifecycleState state() const { return state_; }
    const ApplicationId& foreground_id() const { return foreground_id_; }
    uint32_t foreground_generation() const { return generation_; }
    esp_err_t last_error() const { return last_error_; }
    bool HasForeground() const { return foreground_ != nullptr; }
    const ApplicationRegistry& registry() const { return registry_; }

private:
    friend class ApplicationContext;

    esp_err_t SwitchTo(const ApplicationId& id, bool allow_fallback);
    esp_err_t EnterLauncherFallback(esp_err_t reason);
    void EnterFailsafe(esp_err_t reason);
    void ExitAndDestroyForeground();
    esp_err_t ProcessCommand();
    esp_err_t ProcessRender();
    SubmitResult SubmitCommand(const AppCommand& command);
    bool SubmitRender(const DirtyRegion& dirty, RenderIntent intent);

    template <typename Callback>
    esp_err_t InvokeCallback(Callback callback) {
        callback_active_ = true;
        const esp_err_t result = callback();
        callback_active_ = false;
        return result;
    }

    ApplicationRegistry registry_;
    ApplicationId launcher_id_{};
    Platform* platform_;
    RuntimeDelegate* delegate_;
    std::unique_ptr<Application> foreground_;
    ApplicationId foreground_id_{};
    ApplicationContext context_;
    CommandArbiter commands_;
    RenderAccumulator renders_;
    LifecycleState state_ = LifecycleState::Absent;
    uint32_t generation_ = 0;
    esp_err_t last_error_ = ESP_OK;
    bool callback_active_ = false;
};

}  // namespace zectrix::app
