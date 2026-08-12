#include "zectrix_application_runtime.h"

#include <cassert>
#include <new>
#include <string>
#include <vector>

namespace zectrix { class Platform {}; }

namespace {

using namespace zectrix::app;

struct Behavior {
    esp_err_t factory_result = ESP_OK;
    esp_err_t enter_result = ESP_OK;
    esp_err_t event_result = ESP_OK;
    esp_err_t render_result = ESP_OK;
    esp_err_t exit_result = ESP_OK;
    bool factory_returns_null = false;
    const char* event_open = nullptr;
    bool event_back = false;
    bool event_home = false;
    bool event_shutdown = false;
    bool event_render = false;
    bool enter_two_renders = false;
    bool idle_home = false;
};

Behavior launcher_behavior;
Behavior clock_behavior;
Behavior broken_behavior;
std::vector<std::string> events;
ApplicationContext* retained_context = nullptr;
RenderRequest last_render;

class FakeApplication final : public Application {
public:
    FakeApplication(const char* name, Behavior& behavior)
        : name_(name), behavior_(&behavior) {}
    ~FakeApplication() override { events.emplace_back("destroy:" + name_); }

    esp_err_t Enter(ApplicationContext& context) override {
        events.emplace_back("enter:" + name_);
        retained_context = &context;
        if (behavior_->enter_two_renders) {
            assert(context.RequestRender({10, 20, 20, 20}, RenderIntent::Fast));
            assert(context.RequestRender({20, 10, 20, 20}, RenderIntent::Quality));
        }
        return behavior_->enter_result;
    }

    esp_err_t HandleEvent(const zectrix::input::InputEvent&,
                          ApplicationContext& context) override {
        events.emplace_back("event-begin:" + name_);
        if (behavior_->event_render) {
            assert(context.RequestRender({0, 0, 10, 10}, RenderIntent::Fast));
        }
        if (behavior_->event_open != nullptr) {
            AppCommand command;
            assert(AppCommand::Open(behavior_->event_open, &command));
            assert(context.RequestCommand(command) == SubmitResult::Accepted);
        }
        if (behavior_->event_home) {
            const SubmitResult expected = behavior_->event_open != nullptr
                                              ? SubmitResult::Superseded
                                              : SubmitResult::Accepted;
            assert(context.RequestCommand(AppCommand::Home()) == expected);
        }
        if (behavior_->event_back) {
            const SubmitResult expected = behavior_->event_open != nullptr
                                              ? SubmitResult::Conflict
                                              : SubmitResult::Accepted;
            assert(context.RequestCommand(AppCommand::Back()) == expected);
        }
        if (behavior_->event_shutdown) {
            const SubmitResult expected =
                (behavior_->event_open != nullptr || behavior_->event_home)
                    ? SubmitResult::Superseded
                    : SubmitResult::Accepted;
            assert(context.RequestCommand(AppCommand::Shutdown()) == expected);
        }
        events.emplace_back("event-end:" + name_);
        return behavior_->event_result;
    }

    esp_err_t HandleIdle(ApplicationContext& context) override {
        events.emplace_back("idle:" + name_);
        if (behavior_->idle_home) {
            assert(context.RequestCommand(AppCommand::Home()) ==
                   SubmitResult::Accepted);
        }
        return ESP_OK;
    }

    esp_err_t Render(const RenderRequest& request) override {
        events.emplace_back("render:" + name_);
        last_render = request;
        return behavior_->render_result;
    }

    esp_err_t Exit() override {
        events.emplace_back("exit:" + name_);
        return behavior_->exit_result;
    }

private:
    std::string name_;
    Behavior* behavior_;
};

esp_err_t Make(const char* name, Behavior& behavior, Application** output) {
    events.emplace_back(std::string("factory:") + name);
    if (output == nullptr) return ESP_ERR_INVALID_ARG;
    *output = nullptr;
    if (behavior.factory_result != ESP_OK) return behavior.factory_result;
    if (behavior.factory_returns_null) return ESP_OK;
    *output = new (std::nothrow) FakeApplication(name, behavior);
    return *output == nullptr ? ESP_ERR_NO_MEM : ESP_OK;
}

esp_err_t MakeLauncher(zectrix::Platform&, const ApplicationRegistry&,
                       ApplicationFactoryContext*,
                       Application** output) {
    return Make("launcher", launcher_behavior, output);
}

class FactoryContext final : public ApplicationFactoryContext {};
FactoryContext factory_context_marker;
esp_err_t MakeWithContext(zectrix::Platform&, const ApplicationRegistry&,
                          ApplicationFactoryContext* context,
                          Application** output) {
    assert(context == &factory_context_marker);
    return Make("launcher", launcher_behavior, output);
}
esp_err_t MakeClock(zectrix::Platform&, const ApplicationRegistry&,
                    ApplicationFactoryContext*,
                    Application** output) {
    return Make("clock", clock_behavior, output);
}
esp_err_t MakeBroken(zectrix::Platform&, const ApplicationRegistry&,
                     ApplicationFactoryContext*,
                     Application** output) {
    return Make("broken", broken_behavior, output);
}

class Delegate final : public RuntimeDelegate {
public:
    esp_err_t Shutdown() override {
        ++shutdown_count;
        events.emplace_back("shutdown");
        return shutdown_result;
    }
    void EnterFailsafe(esp_err_t reason) override {
        ++failsafe_count;
        failsafe_reason = reason;
        events.emplace_back("failsafe");
    }

    int shutdown_count = 0;
    int failsafe_count = 0;
    esp_err_t shutdown_result = ESP_OK;
    esp_err_t failsafe_reason = ESP_OK;
};

constexpr ApplicationDescriptor kDescriptors[] = {
    {"launcher", "Launcher", MakeLauncher},
    {"clock", "Clock", MakeClock},
    {"broken", "Broken", MakeBroken},
};

void Reset() {
    launcher_behavior = {};
    clock_behavior = {};
    broken_behavior = {};
    events.clear();
    retained_context = nullptr;
    last_render = {};
}

bool IsForeground(const ApplicationRuntime& runtime, const char* id) {
    ApplicationId expected;
    assert(ApplicationId::Copy(id, &expected));
    return runtime.foreground_id() == expected;
}

}  // namespace

int main() {
    using namespace zectrix::app;
    const zectrix::input::InputEvent input{};
    zectrix::Platform platform;

    {
        Delegate delegate;
        constexpr ApplicationDescriptor invalid[] = {
            {"launcher", "Launcher", nullptr},
        };
        ApplicationRuntime runtime(invalid, 1, "launcher", platform, delegate);
        assert(runtime.Start() == ESP_ERR_INVALID_ARG);
        assert(delegate.shutdown_count == 0);
    }
    {
        Delegate delegate;
        constexpr ApplicationDescriptor duplicate[] = {
            {"launcher", "Launcher", MakeLauncher},
            {"launcher", "Other", MakeClock},
        };
        ApplicationRuntime runtime(duplicate, 2, "launcher", platform, delegate);
        assert(runtime.Start() == ESP_ERR_INVALID_STATE);
    }
    {
        Delegate delegate;
        ApplicationRuntime runtime(kDescriptors, 3, "missing", platform, delegate);
        assert(runtime.Start() == ESP_ERR_NOT_FOUND);
    }

    Reset();
    {
        Delegate delegate;
        launcher_behavior.factory_returns_null = true;
        ApplicationRuntime runtime(kDescriptors, 3, "launcher", platform, delegate);
        assert(runtime.Start() == ESP_ERR_NO_MEM);
        assert(runtime.state() == LifecycleState::Failsafe);
        assert(delegate.failsafe_count == 1);
    }

    Reset();
    {
        Delegate delegate;
        ApplicationRuntime runtime(kDescriptors, 3, "launcher", platform, delegate);
        assert(runtime.Start() == ESP_OK);
        assert(runtime.Start() == ESP_OK);
        assert(runtime.state() == LifecycleState::Active);
        assert(IsForeground(runtime, "launcher"));
        assert(runtime.foreground_generation() == 1);
        assert(runtime.registry().size() == 3);
        assert(std::string(runtime.registry().At(0)->id) == "launcher");
        assert(std::string(runtime.registry().At(1)->id) == "clock");
        assert(runtime.registry().At(3) == nullptr);
        assert(retained_context->RequestCommand(AppCommand::Home()) ==
               SubmitResult::Rejected);

        launcher_behavior.event_open = "clock";
        launcher_behavior.event_render = true;
        assert(runtime.Step(&input) == ESP_OK);
        assert(IsForeground(runtime, "clock"));
        assert(runtime.foreground_generation() == 2);
        assert((events == std::vector<std::string>{
            "factory:launcher", "enter:launcher", "event-begin:launcher",
            "event-end:launcher", "factory:clock", "exit:launcher",
            "destroy:launcher", "enter:clock"}));
    }


    Reset();
    {
        Delegate delegate;
        const ApplicationDescriptor descriptors[] = {
            {"launcher", "Launcher", MakeWithContext,
             &factory_context_marker},
        };
        ApplicationRuntime runtime(descriptors, 1, "launcher", platform,
                                   delegate);
        assert(runtime.Start() == ESP_OK);
        launcher_behavior.idle_home = true;
        assert(runtime.Idle() == ESP_OK);
        assert(IsForeground(runtime, "launcher"));
        assert((events == std::vector<std::string>{
            "factory:launcher", "enter:launcher", "idle:launcher",
            "factory:launcher", "exit:launcher", "destroy:launcher",
            "enter:launcher"}));
    }

    Reset();
    {
        Delegate delegate;
        ApplicationRuntime runtime(kDescriptors, 3, "launcher", platform, delegate);
        assert(runtime.Start() == ESP_OK);
        launcher_behavior.event_open = "clock";
        assert(runtime.Step(&input) == ESP_OK);
        clock_behavior.event_back = true;
        assert(runtime.Step(&input) == ESP_OK);
        assert(IsForeground(runtime, "launcher"));
    }

    Reset();
    {
        Delegate delegate;
        ApplicationRuntime runtime(kDescriptors, 3, "launcher", platform, delegate);
        assert(runtime.Start() == ESP_OK);
        launcher_behavior.event_open = "clock";
        clock_behavior.factory_result = ESP_ERR_NO_MEM;
        assert(runtime.Step(&input) == ESP_ERR_NO_MEM);
        assert(IsForeground(runtime, "launcher"));
        assert(runtime.foreground_generation() == 1);
    }

    Reset();
    {
        Delegate delegate;
        ApplicationRuntime runtime(kDescriptors, 3, "launcher", platform, delegate);
        assert(runtime.Step(&input) == ESP_ERR_INVALID_STATE);
        assert(runtime.Start() == ESP_OK);
        launcher_behavior.event_open = "clock";
        launcher_behavior.event_home = true;
        assert(runtime.Step(&input) == ESP_OK);
        assert(IsForeground(runtime, "launcher"));
    }

    Reset();
    {
        Delegate delegate;
        ApplicationRuntime runtime(kDescriptors, 3, "launcher", platform, delegate);
        assert(runtime.Start() == ESP_OK);
        launcher_behavior.event_open = "broken";
        broken_behavior.enter_result = ESP_FAIL;
        assert(runtime.Step(&input) == ESP_OK);
        assert(IsForeground(runtime, "launcher"));
        assert(runtime.foreground_generation() == 3);
        assert(delegate.failsafe_count == 0);
        bool exited_broken = false;
        bool destroyed_broken = false;
        for (const auto& event : events) {
            exited_broken = exited_broken || event == "exit:broken";
            destroyed_broken = destroyed_broken || event == "destroy:broken";
        }
        assert(exited_broken && destroyed_broken);
    }

    Reset();
    {
        Delegate delegate;
        ApplicationRuntime runtime(kDescriptors, 3, "launcher", platform, delegate);
        assert(runtime.Start() == ESP_OK);
        launcher_behavior.event_open = "broken";
        broken_behavior.enter_result = ESP_FAIL;
        launcher_behavior.factory_result = ESP_ERR_NO_MEM;
        assert(runtime.Step(&input) == ESP_ERR_NO_MEM);
        assert(runtime.state() == LifecycleState::Failsafe);
        assert(!runtime.HasForeground());
        assert(delegate.failsafe_count == 1);
    }

    Reset();
    {
        Delegate delegate;
        ApplicationRuntime runtime(kDescriptors, 3, "launcher", platform, delegate);
        launcher_behavior.enter_two_renders = true;
        assert(runtime.Start() == ESP_OK);
        assert(runtime.Step() == ESP_OK);
        assert(last_render.owner_generation == 1);
        assert(last_render.intent == RenderIntent::Quality);
        assert(last_render.dirty.x == 10 && last_render.dirty.y == 10);
        assert(last_render.dirty.width == 30 && last_render.dirty.height == 30);
    }

    Reset();
    {
        Delegate delegate;
        ApplicationRuntime runtime(kDescriptors, 3, "launcher", platform, delegate);
        launcher_behavior.enter_two_renders = true;
        launcher_behavior.render_result = ESP_FAIL;
        assert(runtime.Start() == ESP_OK);
        assert(runtime.Step() == ESP_FAIL);
        assert(runtime.state() == LifecycleState::Active);
        assert(runtime.last_error() == ESP_FAIL);
    }

    Reset();
    {
        Delegate delegate;
        ApplicationRuntime runtime(kDescriptors, 3, "launcher", platform, delegate);
        assert(runtime.Start() == ESP_OK);
        launcher_behavior.exit_result = ESP_FAIL;
        launcher_behavior.event_home = true;
        assert(runtime.Step(&input) == ESP_OK);
        assert(IsForeground(runtime, "launcher"));
        int destroy_count = 0;
        for (const auto& event : events) {
            if (event == "destroy:launcher") ++destroy_count;
        }
        assert(destroy_count == 1);
    }

    Reset();
    {
        Delegate delegate;
        ApplicationRuntime runtime(kDescriptors, 3, "launcher", platform, delegate);
        assert(runtime.Start() == ESP_OK);
        launcher_behavior.event_open = "unknown";
        assert(runtime.Step(&input) == ESP_ERR_NOT_FOUND);
        assert(IsForeground(runtime, "launcher"));
    }

    Reset();
    {
        Delegate delegate;
        ApplicationRuntime runtime(kDescriptors, 3, "launcher", platform, delegate);
        assert(runtime.Start() == ESP_OK);
        launcher_behavior.event_shutdown = true;
        assert(runtime.Step(&input) == ESP_OK);
        assert(runtime.state() == LifecycleState::Stopped);
        assert(delegate.shutdown_count == 1);
        assert(runtime.Stop() == ESP_OK);
        assert(delegate.shutdown_count == 1);
        assert(runtime.Start() == ESP_ERR_INVALID_STATE);
    }
}
