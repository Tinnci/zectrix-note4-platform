#include "zectrix/sdk/application.h"
#include "zectrix_application_catalog.h"
#include "zectrix_foreground_dispatch.h"

#include <cassert>
#include <deque>
#include <new>
#include <string>
#include <vector>

namespace {

namespace sdk = zectrix::sdk;

void CheckReentry(sdk::ApplicationRuntime* runtime) {
    if (runtime == nullptr) return;
    const sdk::InputEvent event{sdk::Button::Down, sdk::InputAction::Click};
    assert(runtime->Start() == sdk::Status::InvalidState);
    assert(runtime->Step(&event) == sdk::Status::InvalidState);
    assert(runtime->DispatchInput(event) == sdk::Status::InvalidState);
    assert(runtime->Idle() == sdk::Status::InvalidState);
    assert(runtime->Stop() == sdk::Status::InvalidState);
}

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
    bool open_on_ok_only = false;
    bool idle_home = false;
    bool factory_returns_null = false;
    int create_count = 0;
    int destroy_count = 0;
    int render_count = 0;
    int event_count = 0;
    int idle_count = 0;
    sdk::DirtyRegion event_dirty{2, 2, 6, 6};
    sdk::RenderIntent event_intent = sdk::RenderIntent::Fast;
    sdk::RenderRequest last_render{};
    sdk::ApplicationRuntime* reenter = nullptr;
};

std::vector<std::string> events;
sdk::ApplicationContext* retained_context = nullptr;

class TestApplication final : public sdk::Application {
public:
    TestApplication(const char* name, Behavior& behavior)
        : name_(name), behavior_(&behavior) {}
    ~TestApplication() override {
        CheckReentry(behavior_->reenter);
        ++behavior_->destroy_count;
        events.push_back(std::string("destroy:") + name_);
    }

    sdk::Status Enter(sdk::ApplicationContext& context) override {
        CheckReentry(behavior_->reenter);
        retained_context = &context;
        events.push_back(std::string("enter:") + name_);
        context.RequestRender({0, 0, 10, 10}, sdk::RenderIntent::Quality);
        return behavior_->enter_result;
    }

    sdk::Status HandleEvent(const sdk::InputEvent& event,
                            sdk::ApplicationContext& context) override {
        events.push_back(std::string("event-begin:") + name_);
        ++behavior_->event_count;
        CheckReentry(behavior_->reenter);
        if (behavior_->event_open != nullptr &&
            (!behavior_->open_on_ok_only || event.button == sdk::Button::Ok)) {
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
            context.RequestRender(behavior_->event_dirty, behavior_->event_intent);
        }
        events.push_back(std::string("event-end:") + name_);
        return behavior_->event_result;
    }

    sdk::Status HandleIdle(sdk::ApplicationContext& context) override {
        CheckReentry(behavior_->reenter);
        events.push_back(std::string("idle:") + name_);
        ++behavior_->idle_count;
        if (behavior_->idle_home) {
            context.RequestCommand(sdk::AppCommand::Home());
        }
        return behavior_->idle_result;
    }

    sdk::Status Render(const sdk::RenderRequest& request) override {
        CheckReentry(behavior_->reenter);
        ++behavior_->render_count;
        behavior_->last_render = request;
        events.push_back(std::string("render:") + name_);
        return behavior_->render_result;
    }

    sdk::Status Exit() override {
        CheckReentry(behavior_->reenter);
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
        CheckReentry(behavior_->reenter);
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
        CheckReentry(reenter);
        ++shutdown_count;
        return shutdown_result;
    }
    void EnterFailsafe(sdk::Status reason) override {
        CheckReentry(reenter);
        ++failsafe_count;
        failsafe_reason = reason;
    }

    int shutdown_count = 0;
    int failsafe_count = 0;
    sdk::Status shutdown_result = sdk::Status::Ok;
    sdk::Status failsafe_reason = sdk::Status::Ok;
    sdk::ApplicationRuntime* reenter = nullptr;
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
    const sdk::InputEvent down{sdk::Button::Down, sdk::InputAction::Click};

    // Every external callback, including cleanup, must return before another
    // runtime operation can change ownership or enter the shutdown delegate.
    for (unsigned failure = 0; failure < 3; ++failure) {
        Delegate delegate;
        sdk::ApplicationRuntime runtime(descriptors, 3, "launcher", delegate);
        launcher.reenter = clock.reenter = delegate.reenter = &runtime;
        launcher.event_render = true;
        if (failure == 1) launcher.factory_result = sdk::Status::NoMemory;
        if (failure == 2) launcher.enter_result = sdk::Status::IoError;
        const auto expected = failure == 1 ? sdk::Status::NoMemory :
                              failure == 2 ? sdk::Status::IoError : sdk::Status::Ok;
        assert(runtime.Start() == expected);
        if (failure == 0) {
            assert(runtime.Step() == sdk::Status::Ok);
            assert(runtime.DispatchInput(down) == sdk::Status::Ok);
            assert(runtime.Idle() == sdk::Status::Ok);
            launcher.event_open = "clock";
            assert(runtime.Step(&input) == sdk::Status::Ok);
            assert(IsForeground(runtime, "clock") && runtime.foreground_generation() == 2);
        } else {
            assert(runtime.state() == sdk::LifecycleState::Failsafe);
            assert(delegate.failsafe_count == 1 && delegate.failsafe_reason == expected);
        }
        assert(delegate.shutdown_count == 0);
        assert(runtime.Stop() == sdk::Status::Ok && runtime.Stop() == sdk::Status::Ok);
        assert(delegate.shutdown_count == 1);
        assert(launcher.destroy_count == (failure == 1 ? 0 : 1));
        assert(clock.destroy_count == (failure == 0 ? 1 : 0));
        Reset(launcher, clock, broken);
    }

    {
        Delegate delegate;
        sdk::ApplicationRuntime runtime(descriptors, 3, "launcher", delegate);
        assert(runtime.DispatchInput(down) == sdk::Status::InvalidState);
        assert(runtime.Start() == sdk::Status::Ok && runtime.Step() == sdk::Status::Ok);
        launcher.event_render = true;
        launcher.reenter = &runtime;
        assert(runtime.DispatchInput(down) == sdk::Status::Ok);
        launcher.event_dirty = {20, 30, 5, 7};
        launcher.event_intent = sdk::RenderIntent::Quality;
        assert(runtime.DispatchInput(down) == sdk::Status::Ok);
        launcher.event_intent = sdk::RenderIntent::Fast;
        assert(runtime.DispatchInput(down) == sdk::Status::Ok);
        assert(launcher.render_count == 1);
        assert(runtime.Idle() == sdk::Status::Ok && launcher.render_count == 2);
        const auto& render = launcher.last_render;
        assert(render.dirty.x == 2 && render.dirty.y == 2);
        assert(render.dirty.width == 23 && render.dirty.height == 35);
        assert(render.intent == sdk::RenderIntent::Quality);
    }
    Reset(launcher, clock, broken);

    {
        Delegate delegate;
        sdk::ApplicationRuntime runtime(descriptors, 3, "launcher", delegate);
        assert(runtime.Start() == sdk::Status::Ok && runtime.Step() == sdk::Status::Ok);
        launcher.event_render = true;
        std::deque<sdk::InputEvent> backlog(zectrix::app::kMaxInputBurst * 2, down);
        const auto poll = [&backlog](sdk::InputEvent* event) {
            if (backlog.empty()) return false;
            *event = backlog.front();
            backlog.pop_front();
            return true;
        };
        sdk::InputEvent event;
        for (int batch = 1; batch <= 2; ++batch) {
            assert(poll(&event));
            assert(zectrix::app::DispatchInputBurst(runtime, event, poll) == sdk::Status::Ok);
            assert(launcher.event_count == batch * static_cast<int>(zectrix::app::kMaxInputBurst));
            assert(launcher.idle_count == batch && launcher.render_count == batch + 1);
        }
        assert(backlog.empty());

        // Confirmation resolves after earlier directions and leaves later input
        // for the newly rendered foreground, without drawing the outgoing one.
        launcher.event_open = "clock";
        launcher.open_on_ok_only = true;
        backlog = {down, input, down};
        assert(zectrix::app::DispatchInputBurst(runtime, down, poll) == sdk::Status::Ok);
        assert(IsForeground(runtime, "clock") && backlog.size() == 1);
        assert(clock.render_count == 1 && clock.event_count == 0 && clock.idle_count == 0);
        assert(launcher.render_count == 3 && launcher.destroy_count == 1);
    }
    Reset(launcher, clock, broken);

    for (int boundary = 0; boundary < 4; ++boundary) {
        Delegate delegate;
        sdk::ApplicationRuntime runtime(descriptors, 3, "launcher", delegate);
        assert(runtime.Start() == sdk::Status::Ok && runtime.Step() == sdk::Status::Ok);
        launcher.event_render = true;
        if (boundary == 0) launcher.event_open = "clock";
        if (boundary == 1) launcher.event_shutdown = true;
        if (boundary == 2) launcher.event_result = sdk::Status::IoError;
        const auto event = boundary == 3
            ? sdk::InputEvent{sdk::Button::Ok, sdk::InputAction::LongPress} : down;
        const auto result = zectrix::app::DispatchInputBurst(runtime, event,
            [](sdk::InputEvent*) { assert(false); return false; });
        assert(result == (boundary == 2 ? sdk::Status::IoError : sdk::Status::Ok));
        assert(launcher.event_count == 1 && launcher.idle_count == 0);
        if (boundary == 0) assert(clock.render_count == 1 && launcher.render_count == 1);
        if (boundary == 1) assert(delegate.shutdown_count == 1 && launcher.render_count == 1);
        if (boundary >= 2) assert(launcher.render_count == 2);
        runtime.Stop();
        Reset(launcher, clock, broken);
    }

    {
        zectrix::app::ApplicationCatalog catalog;
        assert(catalog.size() == 0 && catalog.menu_size() == 0 && !catalog.MenuAt(0));
        assert(catalog.Add("launcher", "Launcher", launcher_factory));
        assert(catalog.Add("clock", "Clock", clock_factory));
        assert(catalog.menu_size() == 1 && !catalog.MenuAt(1));
        Delegate delegate;
        sdk::ApplicationRuntime runtime(catalog.data(), catalog.size(), "launcher", delegate);
        assert(runtime.Start() == sdk::Status::Ok);
        // The exact same descriptor supplies the menu label and open target.
        assert(std::string(catalog.MenuAt(0)->display_name) == "Clock");
        launcher.event_open = catalog.MenuAt(0)->id;
        assert(runtime.Step(&input) == sdk::Status::Ok && IsForeground(runtime, "clock"));
        sdk::ApplicationId missing;
        assert(sdk::ApplicationId::Copy("reader", &missing));
        assert(!runtime.registry().Find(missing));
        assert(runtime.Stop() == sdk::Status::Ok);
    }
    Reset(launcher, clock, broken);

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
