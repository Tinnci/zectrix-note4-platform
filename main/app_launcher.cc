#include "terminal_internal.h"

#include "esp_log.h"
#include "zectrix_first_party_app_controllers.h"
#include "zectrix_storage_service.h"

namespace zectrix::terminal {

constexpr int64_t kHomeIdleTimeoutUs = 15000000;

class TerminalApp::LauncherApplication final : public sdk::Application {
public:
    explicit LauncherApplication(TerminalApp& owner)
        : owner_(&owner), controller_(owner.applications_), resume_parent_(owner.launcher_back_requested_) {}

    sdk::Status Enter(sdk::ApplicationContext& context) override {
        owner_->BindScenes(controller_);
        uint32_t stored = zectrix::app::kAutoShowcaseDefault;
        const esp_err_t read = owner_->storage_->GetUInt32(
            zectrix::app::kAutoShowcaseSettingKey, &stored);
        bool valid = read == ESP_OK &&
            zectrix::app::NormalizeAutoShowcaseSetting(
                stored, &auto_showcase_);
        if (!valid) {
            auto_showcase_ = zectrix::app::kAutoShowcaseDefault != 0;
            if (read == ESP_ERR_NOT_FOUND || read == ESP_OK) {
                const esp_err_t repair = owner_->storage_->SetUInt32(
                    zectrix::app::kAutoShowcaseSettingKey,
                    zectrix::app::kAutoShowcaseDefault);
                if (repair != ESP_OK) {
                    ESP_LOGW(kTag, "default setting save failed: %s",
                             esp_err_to_name(repair));
                }
            }
        }
        reading_ = owner_->ReadReadingOverview();
        last_input_us_ = owner_->time_->MonotonicMicroseconds();
        auto selection = owner_->launcher_selection_;
        if (!resume_parent_) selection.scene = app::LauncherScene::Home;
        const auto result = controller_.Start(selection);
        return sdk::IsOk(result) ? Apply(controller_.Tick(), context) : result;
    }
    sdk::Status HandleEvent(const sdk::InputEvent& event,
                          sdk::ApplicationContext& context) override {
        last_input_us_ = owner_->time_->MonotonicMicroseconds();
        return Apply(controller_.Handle(event), context);
    }
    sdk::Status HandleIdle(sdk::ApplicationContext& context) override {
        const int64_t now = owner_->time_->MonotonicMicroseconds();
        if (controller_.scene() == app::LauncherScene::Home &&
            auto_showcase_ && owner_->platform_.Health().AutomaticAppsAllowed() &&
            now - last_input_us_ >= kHomeIdleTimeoutUs) {
            last_input_us_ = now;
            return Apply({app::LauncherDecision::OpenSelected, "showcase"}, context);
        }
        if (controller_.scene() == app::LauncherScene::Home &&
            app::LauncherDateChanged(clock_, owner_->time_->Now())) controller_.Invalidate();
        return Apply(controller_.Tick(), context);
    }
    sdk::Status Render(const sdk::RenderRequest& request) override {
        clock_ = owner_->time_->Now();
        const auto result = owner_->ui_.ShowLauncher(controller_, clock_, reading_,
            request.intent == sdk::RenderIntent::Quality);
        controller_.Presented(result == ESP_OK);
        return ToSdkStatus(result);
    }
    sdk::Status Exit() override {
        owner_->launcher_selection_ = controller_.selection();
        controller_.Stop();
        return sdk::Status::Ok;
    }

private:
    sdk::Status Apply(app::LauncherResult result, sdk::ApplicationContext& context) {
        using Decision = app::LauncherDecision;
        sdk::AppCommand command;
        switch (result.decision) {
            case Decision::RenderFast:
            case Decision::RenderQuality:
                return context.RequestRender({0, 24, 400, 276}, result.decision == Decision::RenderQuality
                    ? sdk::RenderIntent::Quality : sdk::RenderIntent::Fast)
                    ? sdk::Status::Ok : sdk::Status::InternalError;
            case Decision::OpenSelected:
            case Decision::ContinueReading:
                if (!sdk::AppCommand::Open(result.target, &command)) return sdk::Status::InvalidState;
                break;
            case Decision::Shutdown:
                command = sdk::AppCommand::Shutdown();
                break;
            case Decision::None: return sdk::Status::Ok;
        }
        const auto submitted = context.RequestCommand(command);
        const bool accepted = submitted == sdk::SubmitResult::Accepted || submitted == sdk::SubmitResult::Superseded;
#if CONFIG_ZECTRIX_ENABLE_READER
        if (accepted) owner_->reader_continue_requested_ = result.decision == Decision::ContinueReading;
#endif
        return accepted ? sdk::Status::Ok : sdk::Status::InvalidState;
    }
    TerminalApp* owner_;
    zectrix::app::LauncherController controller_;
    app::ReadingOverview reading_{};
    time::ClockSnapshot clock_{};
    bool auto_showcase_ = false;
    bool resume_parent_ = false;
    int64_t last_input_us_ = 0;
};

sdk::Status TerminalApp::CreateLauncher(TerminalApp& owner, sdk::Application** output) {
    const auto result = CreateApplication<LauncherApplication>(owner, output);
    // The candidate owns the return mode, including a failed allocation.
    owner.launcher_back_requested_ = false;
    return result;
}

}  // namespace zectrix::terminal
