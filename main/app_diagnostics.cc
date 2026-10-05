#include "note4_locale.h"
#include "terminal_internal.h"

#include <iterator>

#include "note4_first_party_app_controllers.h"
#include "note4_log_event.h"

using note4::i18n::Tr;
using note4::i18n::Text;

namespace note4::terminal {

class TerminalApp::DiagnosticsApplication final : public sdk::Application {
public:
    explicit DiagnosticsApplication(TerminalApp& owner) : owner_(&owner) {}

    sdk::Status Enter(sdk::ApplicationContext& context) override {
        owner_->BindScenes(controller_);
        owner_->test_states_.fill(Note4TestState::kWait);
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
        if (controller_.page() == note4::app::DiagnosticsPage::Summary) {
            result = owner_->ui_.ShowTestSummary(owner_->test_states_);
        } else if (controller_.page() == note4::app::DiagnosticsPage::Individual) {
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

    Note4TestResult Execute(Note4TestId id) {
        owner_->test_states_[static_cast<size_t>(id)] =
            Note4TestState::kRunning;
        const auto result = owner_->tests_->Run(
            id, [this](const Note4TestUpdate& update) {
                owner_->UpdateSystemStatus();
                owner_->test_states_[static_cast<size_t>(update.id)] =
                    update.state;
                const esp_err_t draw = owner_->ui_.ShowTestUpdate(
                    update, owner_->test_states_);
                if (draw != ESP_OK) {
                    NOTE4_LOGE(kTag, "diagnostic_update_failed", "error=%s",
                               note4::log::Token(esp_err_to_name(draw)).c_str());
                }
            });
        // Each interactive item has its own bounded deadline. Polling inside an
        // item must not conceal a hung test or driver from the RTC watchdog.
        owner_->platform_.Health().Progress();
        return result;
    }

    sdk::Status RunAll(sdk::ApplicationContext& context) {
        owner_->test_states_.fill(Note4TestState::kWait);
        bool cancelled = false;
        for (size_t index = 0;
             index < static_cast<size_t>(Note4TestId::kCount); ++index) {
            esp_err_t draw = owner_->ui_.ShowTestMenu(
                index, owner_->test_states_, true);
            if (draw != ESP_OK) {
                // Unwind Running even when a progress frame cannot be shown.
                Apply(controller_.FinishRun(true), context);
                return ToSdkStatus(draw);
            }
            const Note4TestResult result =
                Execute(static_cast<Note4TestId>(index));
            if (result == Note4TestResult::kShutdown) {
                context.RequestCommand(sdk::AppCommand::Shutdown());
                return sdk::Status::Ok;
            }
            RecordResult(index, result);
            if (result == Note4TestResult::kCancelled) {
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
        const Note4TestResult result =
            Execute(static_cast<Note4TestId>(selected));
        if (result == Note4TestResult::kShutdown) {
            context.RequestCommand(sdk::AppCommand::Shutdown());
            return sdk::Status::Ok;
        }
        RecordResult(selected, result);
        const bool cancelled = result == Note4TestResult::kCancelled;
        if (!cancelled && owner_->Wait(1200, true) == ControlResult::kShutdown) {
            context.RequestCommand(sdk::AppCommand::Shutdown());
            return sdk::Status::Ok;
        }
        return Apply(controller_.FinishRun(cancelled), context);
    }

    void RecordResult(size_t index, Note4TestResult result) {
        auto& state = owner_->test_states_[index];
        switch (result) {
            case Note4TestResult::kPass: state = Note4TestState::kPass; break;
            case Note4TestResult::kFail: state = Note4TestState::kFail; break;
            case Note4TestResult::kSkipped: state = Note4TestState::kSkipped; break;
            case Note4TestResult::kCancelled: state = Note4TestState::kWait; break;
            case Note4TestResult::kShutdown: break;
        }
    }

    TerminalApp* owner_;
    note4::app::DiagnosticsController controller_;
};

sdk::Status TerminalApp::CreateDiagnostics(TerminalApp& owner, sdk::Application** output) {
    return CreateApplication<DiagnosticsApplication>(owner, output);
}

}  // namespace note4::terminal
