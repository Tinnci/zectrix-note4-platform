#include "zectrix/zectrix_sdk.h"

#include <new>

namespace sdk = zectrix::sdk;

class MinimalApplication final : public sdk::Application {
public:
    sdk::Status Enter(sdk::ApplicationContext& context) override {
        return context.RequestRender({0, 0, 400, 300},
                                     sdk::RenderIntent::Quality)
                   ? sdk::Status::Ok
                   : sdk::Status::InternalError;
    }

    sdk::Status HandleEvent(const sdk::InputEvent& event,
                            sdk::ApplicationContext& context) override {
        if (event.button == sdk::Button::Ok &&
            event.action == sdk::InputAction::LongPress) {
            context.RequestCommand(sdk::AppCommand::Home());
        }
        return sdk::Status::Ok;
    }

    sdk::Status Render(const sdk::RenderRequest&) override {
        // Compose the view through dependencies owned by the firmware.
        return sdk::Status::Ok;
    }

    sdk::Status Exit() override { return sdk::Status::Ok; }
};

class MinimalFactory final : public sdk::ApplicationFactory {
public:
    sdk::Status Create(const sdk::ApplicationRegistry&,
                       sdk::Application** output) override {
        if (output == nullptr) return sdk::Status::InvalidArgument;
        *output = new (std::nothrow) MinimalApplication();
        return *output == nullptr ? sdk::Status::NoMemory : sdk::Status::Ok;
    }
};
