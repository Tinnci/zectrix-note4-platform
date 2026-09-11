#include "zectrix_locale.h"
#include "terminal_internal.h"

#include <iterator>

#include "esp_log.h"
#include "zectrix_first_party_app_controllers.h"


using zectrix::i18n::Tr;
using zectrix::i18n::Text;

namespace zectrix::terminal {

class TerminalApp::DiagnosticsApplication final : public sdk::Application {
public:
    explicit DiagnosticsApplication(TerminalApp& owner) : owner_(&owner) {}

    sdk::Status Enter(sdk::ApplicationContext& context) override {
        owner_->BindScenes(controller_);
        owner_->test_states_.fill(ZectrixTestState::kWait);
        const auto started = controller_.Start();
        return sdk::IsOk(started) ? Apply(controller_.Tick(), context) : started;
    }

    sdk::Status HandleEvent(const sdk::InputEvent& event,
                          sdk::ApplicationContext& context) override {
        return Apply(controller_.Handle(event), context);
    }

    sdk::Status HandleIdle(sdk::ApplicationContext& context) override {
        return Apply(controller_.Tick(), context);
    }

    sdk::Status Render(const sdk::RenderRequest& request) override {
        esp_err_t result;
        if (controller_.page() == zectrix::app::DiagnosticsPage::Summary) {
            result = owner_->ui_.ShowTestSummary(owner_->test_states_);
        } else if (controller_.page() == zectrix::app::DiagnosticsPage::Individual) {
            result = owner_->ui_.ShowTestMenu(
                controller_.selected(), owner_->test_states_,
                request.intent == sdk::RenderIntent::Quality);
        } else {
            const char* kItems[] = {
                Tr(Text::RunAllTests), Tr(Text::SelectTest)};
            result = owner_->ui_.ShowMenu(
                Tr(Text::HardwareTests), kItems, std::size(kItems),
                controller_.selected(), Tr(Text::NavSelectBack),
                request.intent == sdk::RenderIntent::Quality);
        }
        controller_.Presented(result == ESP_OK);
        return ToSdkStatus(result);
    }

    sdk::Status Exit() override { controller_.Stop(); return sdk::Status::Ok; }

private:
    sdk::Status Apply(app::DiagnosticsResult result, sdk::ApplicationContext& context) {
        using Decision = app::DiagnosticsDecision;
        switch (result.decision) {
            case Decision::RenderFast:
            case Decision::RenderQuality:
                return context.RequestRender({0, 24, 400, 276}, result.decision == Decision::RenderQuality
                    ? sdk::RenderIntent::Quality : sdk::RenderIntent::Fast)
                    ? sdk::Status::Ok : sdk::Status::InternalError;
            case Decision::Back: return owner_->RequestBack(context);
            case Decision::Shutdown:
                context.RequestCommand(sdk::AppCommand::Shutdown());
                return sdk::Status::Ok;
            case Decision::RunAll: return RunAll(context);
            case Decision::RunSelected: return RunSelected(result.selected, context);
            case Decision::None: return sdk::Status::Ok;
        }
        return sdk::Status::InternalError;
    }

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
        bool cancelled = false;
        for (size_t index = 0;
             index < static_cast<size_t>(ZectrixTestId::kCount); ++index) {
            esp_err_t draw = owner_->ui_.ShowTestMenu(
                index, owner_->test_states_, true);
            if (draw != ESP_OK) {
                // Unwind Running even when a progress frame cannot be shown.
                Apply(controller_.FinishRun(true), context);
                return ToSdkStatus(draw);
            }
            const ZectrixTestResult result =
                Execute(static_cast<ZectrixTestId>(index));
            if (result == ZectrixTestResult::kShutdown) {
                context.RequestCommand(sdk::AppCommand::Shutdown());
                return sdk::Status::Ok;
            }
            RecordResult(index, result);
            if (result == ZectrixTestResult::kCancelled) {
                cancelled = true;
                break;
            }
            const auto control = owner_->Wait(800, false);
            if (control == ControlResult::kShutdown) {
                context.RequestCommand(sdk::AppCommand::Shutdown());
                return sdk::Status::Ok;
            }
            if (control == ControlResult::kBack) {
                cancelled = true;
                break;
            }
        }
        return Apply(controller_.FinishRun(cancelled), context);
    }

    sdk::Status RunSelected(size_t selected,
                          sdk::ApplicationContext& context) {
        const ZectrixTestResult result =
            Execute(static_cast<ZectrixTestId>(selected));
        if (result == ZectrixTestResult::kShutdown) {
            context.RequestCommand(sdk::AppCommand::Shutdown());
            return sdk::Status::Ok;
        }
        RecordResult(selected, result);
        const bool cancelled = result == ZectrixTestResult::kCancelled;
        if (!cancelled && owner_->Wait(1200, true) == ControlResult::kShutdown) {
            context.RequestCommand(sdk::AppCommand::Shutdown());
            return sdk::Status::Ok;
        }
        return Apply(controller_.FinishRun(cancelled), context);
    }

    void RecordResult(size_t index, ZectrixTestResult result) {
        auto& state = owner_->test_states_[index];
        switch (result) {
            case ZectrixTestResult::kPass: state = ZectrixTestState::kPass; break;
            case ZectrixTestResult::kFail: state = ZectrixTestState::kFail; break;
            case ZectrixTestResult::kSkipped: state = ZectrixTestState::kSkipped; break;
            case ZectrixTestResult::kCancelled: state = ZectrixTestState::kWait; break;
            case ZectrixTestResult::kShutdown: break;
        }
    }

    TerminalApp* owner_;
    zectrix::app::DiagnosticsController controller_;
};

sdk::Status TerminalApp::CreateDiagnostics(TerminalApp& owner, sdk::Application** output) {
    return CreateApplication<DiagnosticsApplication>(owner, output);
}

}  // namespace zectrix::terminal
