#include "terminal_internal.h"

#include <iterator>

#include "esp_log.h"
#include "zectrix_first_party_app_controllers.h"


namespace zectrix::terminal {

class TerminalApp::DiagnosticsApplication final : public sdk::Application {
public:
    explicit DiagnosticsApplication(TerminalApp& owner) : owner_(&owner) {}

    sdk::Status Enter(sdk::ApplicationContext& context) override {
        owner_->test_states_.fill(ZectrixTestState::kWait);
        return context.RequestRender({0, 0, 400, 300},
                                     sdk::RenderIntent::Quality)
                   ? sdk::Status::Ok : sdk::Status::InternalError;
    }

    sdk::Status HandleEvent(const sdk::InputEvent& event,
                          sdk::ApplicationContext& context) override {
        const zectrix::app::DiagnosticsResult result = controller_.Handle(event);
        if (result.decision == zectrix::app::DiagnosticsDecision::RenderFast) {
            context.RequestRender({0, 24, 400, 276},
                                  sdk::RenderIntent::Fast);
            return sdk::Status::Ok;
        }
        if (result.decision == zectrix::app::DiagnosticsDecision::Home) {
            context.RequestCommand(sdk::AppCommand::Home());
            return sdk::Status::Ok;
        }
        if (result.decision == zectrix::app::DiagnosticsDecision::Shutdown) {
            context.RequestCommand(sdk::AppCommand::Shutdown());
            return sdk::Status::Ok;
        }
        if (result.decision == zectrix::app::DiagnosticsDecision::RunAll) {
            return RunAll(context);
        }
        if (result.decision ==
            zectrix::app::DiagnosticsDecision::RunSelected) {
            return RunSelected(result.selected, context);
        }
        return sdk::Status::Ok;
    }

    sdk::Status Render(const sdk::RenderRequest& request) override {
        if (controller_.page() == zectrix::app::DiagnosticsPage::Summary) {
            return ToSdkStatus(
                owner_->ui_.ShowTestSummary(owner_->test_states_));
        }
        if (controller_.page() == zectrix::app::DiagnosticsPage::Individual) {
            return ToSdkStatus(owner_->ui_.ShowTestMenu(
                controller_.selected(), owner_->test_states_,
                request.intent == sdk::RenderIntent::Quality));
        }
        static constexpr const char* kItems[] = {
            "RUN ALL TESTS", "SELECT INDIVIDUAL TEST"};
        return ToSdkStatus(owner_->ui_.ShowMenu(
            "HARDWARE TESTS", kItems, std::size(kItems),
            controller_.selected(),
            "UP/DOWN Move  OK Select  Hold OK Home",
            request.intent == sdk::RenderIntent::Quality));
    }

    sdk::Status Exit() override { return sdk::Status::Ok; }

private:
    ZectrixTestResult Execute(ZectrixTestId id) {
        owner_->test_states_[static_cast<size_t>(id)] =
            ZectrixTestState::kRunning;
        return owner_->tests_->Run(
            id, [this](const ZectrixTestUpdate& update) {
                owner_->UpdateSystemStatus();
                owner_->test_states_[static_cast<size_t>(update.id)] =
                    update.state;
                const esp_err_t draw = owner_->ui_.ShowTestUpdate(
                    update, owner_->test_states_);
                if (draw != ESP_OK) {
                    ESP_LOGE(kTag, "diagnostic update failed: %s",
                             esp_err_to_name(draw));
                }
            });
    }

    sdk::Status RunAll(sdk::ApplicationContext& context) {
        owner_->test_states_.fill(ZectrixTestState::kWait);
        for (size_t index = 0;
             index < static_cast<size_t>(ZectrixTestId::kCount); ++index) {
            esp_err_t draw = owner_->ui_.ShowTestMenu(
                index, owner_->test_states_, true);
            if (draw != ESP_OK) return ToSdkStatus(draw);
            const ZectrixTestResult result =
                Execute(static_cast<ZectrixTestId>(index));
            if (result == ZectrixTestResult::kShutdown) {
                context.RequestCommand(sdk::AppCommand::Shutdown());
                return sdk::Status::Ok;
            }
            if (result == ZectrixTestResult::kCancelled) break;
            if (result == ZectrixTestResult::kSkipped) {
                owner_->test_states_[index] = ZectrixTestState::kSkipped;
            } else {
                owner_->test_states_[index] = result == ZectrixTestResult::kPass
                    ? ZectrixTestState::kPass : ZectrixTestState::kFail;
            }
            if (owner_->Wait(800, false) ==
                ControlResult::kShutdown) {
                context.RequestCommand(sdk::AppCommand::Shutdown());
                return sdk::Status::Ok;
            }
        }
        controller_.ShowSummary();
        return ToSdkStatus(
            owner_->ui_.ShowTestSummary(owner_->test_states_));
    }

    sdk::Status RunSelected(size_t selected,
                          sdk::ApplicationContext& context) {
        const ZectrixTestResult result =
            Execute(static_cast<ZectrixTestId>(selected));
        if (result == ZectrixTestResult::kShutdown) {
            context.RequestCommand(sdk::AppCommand::Shutdown());
            return sdk::Status::Ok;
        }
        if (result == ZectrixTestResult::kPass) {
            owner_->test_states_[selected] = ZectrixTestState::kPass;
        } else if (result == ZectrixTestResult::kFail) {
            owner_->test_states_[selected] = ZectrixTestState::kFail;
        } else if (result == ZectrixTestResult::kSkipped) {
            owner_->test_states_[selected] = ZectrixTestState::kSkipped;
        }
        if (owner_->Wait(1200, true) ==
            ControlResult::kShutdown) {
            context.RequestCommand(sdk::AppCommand::Shutdown());
            return sdk::Status::Ok;
        }
        return ToSdkStatus(owner_->ui_.ShowTestMenu(
            selected, owner_->test_states_, true));
    }

    TerminalApp* owner_;
    zectrix::app::DiagnosticsController controller_;
};

sdk::Status TerminalApp::CreateDiagnostics(TerminalApp& owner, sdk::Application** output) {
    return CreateApplication<DiagnosticsApplication>(owner, output);
}

}  // namespace zectrix::terminal
