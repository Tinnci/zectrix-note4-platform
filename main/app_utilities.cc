#include "terminal_internal.h"
#include "zectrix_utilities.h"

namespace zectrix::terminal {

class TerminalApp::UtilitiesApplication final : public sdk::Application {
public:
    explicit UtilitiesApplication(TerminalApp& owner) : owner_(owner), controller_(owner.utilities_) {}

    sdk::Status Enter(sdk::ApplicationContext& context) override {
        const auto started = controller_.Start(owner_.time_->MonotonicMicroseconds(), owner_.time_->Now());
        if (!sdk::IsOk(started)) return started;
        return HandleIdle(context);
    }
    sdk::Status HandleEvent(const sdk::InputEvent& event, sdk::ApplicationContext& context) override {
        return Apply(controller_.Handle(event, owner_.time_->MonotonicMicroseconds(), owner_.time_->Now()), context);
    }
    sdk::Status HandleIdle(sdk::ApplicationContext& context) override {
        return Apply(controller_.Tick(owner_.time_->MonotonicMicroseconds(), owner_.time_->Now()), context);
    }
    sdk::Status Render(const sdk::RenderRequest& request) override {
        const auto result = owner_.ui_.ShowUtilities(controller_, request.intent == sdk::RenderIntent::Quality);
        controller_.Presented(result == ESP_OK);
        return ToSdkStatus(result);
    }
    sdk::Status Exit() override { controller_.Stop(); return sdk::Status::Ok; }

private:
    sdk::Status Apply(app::UtilityDecision decision, sdk::ApplicationContext& context) {
        using Decision = app::UtilityDecision;
        if (decision == Decision::Back) return owner_.RequestBack(context);
        if (decision == Decision::Shutdown) {
            const auto submitted = context.RequestCommand(sdk::AppCommand::Shutdown());
            return submitted == sdk::SubmitResult::Accepted || submitted == sdk::SubmitResult::Superseded
                ? sdk::Status::Ok : sdk::Status::InternalError;
        }
        if (decision == Decision::RenderFast || decision == Decision::RenderQuality) {
            const auto requested = context.RequestRender({0, 24, 400, 276}, decision == Decision::RenderQuality
                ? sdk::RenderIntent::Quality : sdk::RenderIntent::Fast);
            if (!requested) controller_.Presented(false);
            return requested ? sdk::Status::Ok : sdk::Status::InternalError;
        }
        return sdk::Status::Ok;
    }
    TerminalApp& owner_;
    app::UtilityController controller_;
};

sdk::Status TerminalApp::CreateUtilities(TerminalApp& owner, sdk::Application** output) {
    return CreateApplication<UtilitiesApplication>(owner, output);
}

}  // namespace zectrix::terminal
