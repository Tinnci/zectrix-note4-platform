#include "zectrix/sdk/application.h"

#include <cassert>
#include <new>
#include <string>
#include <vector>

namespace {

namespace sdk = zectrix::sdk;

struct Behavior {
    sdk::Status factory_result = sdk::Status::Ok;
    sdk::Status enter_result = sdk::Status::Ok;
    sdk::Status event_result = sdk::Status::Ok;
    sdk::Status idle_result = sdk::Status::Ok;
    sdk::Status render_result = sdk::Status::Ok;
    sdk::Status exit_result = sdk::Status::Ok;
    const char* event_open = nullptr;
    bool event_home = false;
    bool event_shutdown = false;
    bool event_render = false;
    bool idle_home = false;
    bool factory_returns_null = false;
    int create_count = 0;
    int destroy_count = 0;
    int render_count = 0;
};

std::vector<std::string> events;
sdk::ApplicationContext* retained_context = nullptr;

class TestApplication final : public sdk::Application {
public:
    TestApplication(const char* name, Behavior& behavior)
        : name_(name), behavior_(&behavior) {}
    ~TestApplication() override {
        ++behavior_->destroy_count;
        events.push_back(std::string("destroy:") + name_);
    }

    sdk::Status Enter(sdk::ApplicationContext& context) override {
        retained_context = &context;
        events.push_back(std::string("enter:") + name_);
        context.RequestRender({0, 0, 10, 10}, sdk::RenderIntent::Quality);
        return behavior_->enter_result;
    }

    sdk::Status HandleEvent(const sdk::InputEvent&,
                            sdk::ApplicationContext& context) override {
        events.push_back(std::string("event-begin:") + name_);
        if (behavior_->event_open != nullptr) {
            sdk::AppCommand command;
            assert(sdk::AppCommand::Open(behavior_->event_open, &command));
            context.RequestCommand(command);
        }
        if (behavior_->event_home) {
            context.RequestCommand(sdk::AppCommand::Home());
        }
        if (behavior_->event_shutdown) {
            context.RequestCommand(sdk::AppCommand::Shutdown());
        }
        if (behavior_->event_render) {
            context.RequestRender({2, 2, 6, 6}, sdk::RenderIntent::Fast);
        }
        events.push_back(std::string("event-end:") + name_);
        return behavior_->event_result;
    }

    sdk::Status HandleIdle(sdk::ApplicationContext& context) override {
        events.push_back(std::string("idle:") + name_);
        if (behavior_->idle_home) {
            context.RequestCommand(sdk::AppCommand::Home());
        }
        return behavior_->idle_result;
    }

    sdk::Status Render(const sdk::RenderRequest&) override {
        ++behavior_->render_count;
        events.push_back(std::string("render:") + name_);
        return behavior_->render_result;
    }

    sdk::Status Exit() override {
        events.push_back(std::string("exit:") + name_);
        return behavior_->exit_result;
    }

private:
    const char* name_;
    Behavior* behavior_;
};

class Factory final : public sdk::ApplicationFactory {
public:
    Factory(const char* name, Behavior& behavior)
        : name_(name), behavior_(&behavior) {}

    sdk::Status Create(const sdk::ApplicationRegistry& registry,
                       sdk::Application** output) override {
        assert(registry.size() > 0);
        if (output == nullptr) return sdk::Status::InvalidArgument;
        *output = nullptr;
        ++behavior_->create_count;
        events.push_back(std::string("factory:") + name_);
        if (!sdk::IsOk(behavior_->factory_result)) {
            return behavior_->factory_result;
        }
        if (behavior_->factory_returns_null) return sdk::Status::Ok;
        *output = new (std::nothrow) TestApplication(name_, *behavior_);
        return *output == nullptr ? sdk::Status::NoMemory : sdk::Status::Ok;
    }

private:
    const char* name_;
    Behavior* behavior_;
};

class Delegate final : public sdk::RuntimeDelegate {
public:
    sdk::Status Shutdown() override {
        ++shutdown_count;
        return shutdown_result;
    }
    void EnterFailsafe(sdk::Status reason) override {
        ++failsafe_count;
        failsafe_reason = reason;
    }

    int shutdown_count = 0;
    int failsafe_count = 0;
    sdk::Status shutdown_result = sdk::Status::Ok;
    sdk::Status failsafe_reason = sdk::Status::Ok;
};

bool IsForeground(const sdk::ApplicationRuntime& runtime, const char* id) {
    sdk::ApplicationId expected;
    return sdk::ApplicationId::Copy(id, &expected) &&
           runtime.foreground_id() == expected;
}

void Reset(Behavior& launcher, Behavior& clock, Behavior& broken) {
    launcher = {};
    clock = {};
    broken = {};
    events.clear();
    retained_context = nullptr;
}

}  // namespace

int main() {
    Behavior launcher;
    Behavior clock;
    Behavior broken;
    Factory launcher_factory("launcher", launcher);
    Factory clock_factory("clock", clock);
    Factory broken_factory("broken", broken);
    const sdk::ApplicationDescriptor descriptors[] = {
        {"launcher", "Launcher", &launcher_factory},
        {"clock", "Clock", &clock_factory},
        {"broken", "Broken", &broken_factory},
    };
    const sdk::InputEvent input{sdk::Button::Ok, sdk::InputAction::Click};

    {
        sdk::ApplicationId launcher_id;
        assert(sdk::ApplicationId::Copy("launcher", &launcher_id));
        sdk::ApplicationRegistry empty(nullptr, 0);
        assert(empty.Validate(launcher_id) == sdk::Status::InvalidArgument);
        const sdk::ApplicationDescriptor duplicate[] = {
            {"launcher", "Launcher", &launcher_factory},
            {"launcher", "Again", &clock_factory},
        };
        sdk::ApplicationRegistry duplicate_registry(duplicate, 2);
        assert(duplicate_registry.Validate(launcher_id) ==
               sdk::Status::InvalidState);
        const sdk::ApplicationDescriptor null_factory[] = {
            {"launcher", "Launcher", nullptr},
        };
        assert(sdk::ApplicationRegistry(null_factory, 1).Validate(launcher_id) ==
               sdk::Status::InvalidArgument);
    }

    Reset(launcher, clock, broken);
    {
        Delegate delegate;
        launcher.factory_returns_null = true;
        sdk::ApplicationRuntime runtime(descriptors, 3, "launcher", delegate);
        assert(runtime.Start() == sdk::Status::NoMemory);
        assert(runtime.state() == sdk::LifecycleState::Failsafe);
        assert(delegate.failsafe_count == 1);
    }

    Reset(launcher, clock, broken);
    {
        Delegate delegate;
        sdk::ApplicationRuntime runtime(descriptors, 3, "launcher", delegate);
        assert(runtime.Start() == sdk::Status::Ok);
        assert(runtime.Start() == sdk::Status::Ok);
        assert(IsForeground(runtime, "launcher"));
        assert(runtime.foreground_generation() == 1);
        assert(runtime.registry().At(3) == nullptr);
        assert(retained_context->RequestCommand(sdk::AppCommand::Home()) ==
               sdk::SubmitResult::Rejected);

        launcher.event_open = "clock";
        launcher.event_render = true;
        assert(runtime.Step(&input) == sdk::Status::Ok);
        assert(IsForeground(runtime, "clock"));
        assert(runtime.foreground_generation() == 2);
        assert((events == std::vector<std::string>{
            "factory:launcher", "enter:launcher", "event-begin:launcher",
            "event-end:launcher", "factory:clock", "exit:launcher",
            "destroy:launcher", "enter:clock", "render:clock"}));
    }

    Reset(launcher, clock, broken);
    {
        Delegate delegate;
        sdk::ApplicationRuntime runtime(descriptors, 3, "launcher", delegate);
        assert(runtime.Start() == sdk::Status::Ok);
        launcher.event_open = "clock";
        clock.factory_result = sdk::Status::NoMemory;
        assert(runtime.Step(&input) == sdk::Status::NoMemory);
        assert(IsForeground(runtime, "launcher"));
        assert(launcher.destroy_count == 0);
    }

    Reset(launcher, clock, broken);
    {
        Delegate delegate;
        sdk::ApplicationRuntime runtime(descriptors, 3, "launcher", delegate);
        assert(runtime.Start() == sdk::Status::Ok);
        launcher.event_open = "broken";
        broken.enter_result = sdk::Status::IoError;
        assert(runtime.Step(&input) == sdk::Status::Ok);
        assert(IsForeground(runtime, "launcher"));
        assert(broken.destroy_count == 1);
        assert(launcher.create_count == 2);
    }

    Reset(launcher, clock, broken);
    {
        Delegate delegate;
        launcher.event_open = "clock";
        launcher.exit_result = sdk::Status::IoError;
        sdk::ApplicationRuntime runtime(descriptors, 3, "launcher", delegate);
        assert(runtime.Start() == sdk::Status::Ok);
        assert(runtime.Step(&input) == sdk::Status::Ok);
        assert(launcher.destroy_count == 1);
        assert(runtime.last_error() == sdk::Status::IoError);
    }

    Reset(launcher, clock, broken);
    {
        Delegate delegate;
        launcher.render_result = sdk::Status::IoError;
        sdk::ApplicationRuntime runtime(descriptors, 3, "launcher", delegate);
        assert(runtime.Start() == sdk::Status::Ok);
        assert(runtime.Step() == sdk::Status::IoError);
        assert(runtime.state() == sdk::LifecycleState::Active);
        assert(runtime.last_error() == sdk::Status::IoError);
    }

    Reset(launcher, clock, broken);
    {
        Delegate delegate;
        sdk::ApplicationRuntime runtime(descriptors, 3, "launcher", delegate);
        assert(runtime.Start() == sdk::Status::Ok);
        launcher.idle_home = true;
        assert(runtime.Idle() == sdk::Status::Ok);
        assert(IsForeground(runtime, "launcher"));
    }

    Reset(launcher, clock, broken);
    {
        Delegate delegate;
        launcher.event_shutdown = true;
        sdk::ApplicationRuntime runtime(descriptors, 3, "launcher", delegate);
        assert(runtime.Start() == sdk::Status::Ok);
        assert(runtime.Step(&input) == sdk::Status::Ok);
        assert(runtime.state() == sdk::LifecycleState::Stopped);
        assert(runtime.Stop() == sdk::Status::Ok);
        assert(delegate.shutdown_count == 1);
        assert(launcher.destroy_count == 1);
    }
}
