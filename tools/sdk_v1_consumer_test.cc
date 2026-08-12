#include "zectrix/zectrix_sdk.h"

#include <cassert>
#include <new>
#include <type_traits>

namespace sdk = zectrix::sdk;

static_assert(ZECTRIX_SDK_VERSION_MAJOR == 1);
static_assert(ZECTRIX_SDK_VERSION_MINOR == 0);
static_assert(ZECTRIX_SDK_VERSION_PATCH == 0);
static_assert(sdk::kVersion.major == 1);
static_assert(static_cast<unsigned>(sdk::Status::Ok) == 0);
static_assert(static_cast<unsigned>(sdk::Status::InternalError) == 10);
static_assert(std::is_same_v<decltype(sdk::InputEvent{}.button), sdk::Button>);

namespace {

class ConsumerApplication final : public sdk::Application {
public:
    sdk::Status Enter(sdk::ApplicationContext& context) override {
        return context.RequestRender({0, 0, 10, 10},
                                     sdk::RenderIntent::Quality)
                   ? sdk::Status::Ok
                   : sdk::Status::InternalError;
    }
    sdk::Status HandleEvent(const sdk::InputEvent&,
                            sdk::ApplicationContext& context) override {
        context.RequestCommand(sdk::AppCommand::Shutdown());
        return sdk::Status::Ok;
    }
    sdk::Status Render(const sdk::RenderRequest&) override {
        ++render_count;
        return sdk::Status::Ok;
    }
    sdk::Status Exit() override {
        ++exit_count;
        return sdk::Status::Ok;
    }

    static int render_count;
    static int exit_count;
};

int ConsumerApplication::render_count = 0;
int ConsumerApplication::exit_count = 0;

class ConsumerFactory final : public sdk::ApplicationFactory {
public:
    sdk::Status Create(const sdk::ApplicationRegistry& registry,
                       sdk::Application** output) override {
        if (output == nullptr || registry.size() != 1) {
            return sdk::Status::InvalidArgument;
        }
        *output = new (std::nothrow) ConsumerApplication();
        return *output == nullptr ? sdk::Status::NoMemory : sdk::Status::Ok;
    }
};

class ConsumerDelegate final : public sdk::RuntimeDelegate {
public:
    sdk::Status Shutdown() override {
        ++shutdown_count;
        return sdk::Status::Ok;
    }
    void EnterFailsafe(sdk::Status) override { assert(false); }
    int shutdown_count = 0;
};

}  // namespace

int main() {
    assert(sdk::IsOk(sdk::Status::Ok));
    assert(!sdk::IsOk(sdk::Status::IoError));
    assert(sdk::StatusName(sdk::Status::Timeout) != nullptr);

    ConsumerFactory factory;
    ConsumerDelegate delegate;
    const sdk::ApplicationDescriptor descriptors[] = {
        {"consumer", "Consumer", &factory},
    };
    sdk::ApplicationRuntime runtime(descriptors, 1, "consumer", delegate);
    assert(runtime.Start() == sdk::Status::Ok);
    assert(runtime.Step() == sdk::Status::Ok);
    assert(ConsumerApplication::render_count == 1);
    const sdk::InputEvent event{sdk::Button::Ok, sdk::InputAction::Click};
    assert(runtime.Step(&event) == sdk::Status::Ok);
    assert(runtime.state() == sdk::LifecycleState::Stopped);
    assert(delegate.shutdown_count == 1);
    assert(ConsumerApplication::exit_count == 1);
}
