#include "terminal_internal.h"

#include "zectrix_first_party_app_controllers.h"
#include "zectrix_storage_service.h"


namespace zectrix::terminal {

class TerminalApp::SettingsApplication final : public sdk::Application {
public:
    explicit SettingsApplication(TerminalApp& owner)
        : owner_(&owner), controller_(zectrix::app::kAutoShowcaseDefault != 0) {}

    sdk::Status Enter(sdk::ApplicationContext& context) override {
        uint32_t stored = zectrix::app::kAutoShowcaseDefault;
        const esp_err_t read = owner_->storage_->GetUInt32(
            zectrix::app::kAutoShowcaseSettingKey, &stored);
        bool value = zectrix::app::kAutoShowcaseDefault != 0;
        bool repair = false;
        if (read == ESP_ERR_NOT_FOUND) {
            status_ = "DEFAULT CREATED";
            repair = true;
        } else if (read != ESP_OK) {
            status_ = "LOAD FAILED - DEFAULT";
        } else if (!zectrix::app::NormalizeAutoShowcaseSetting(stored,
                                                               &value)) {
            status_ = "INVALID RESET";
            value = zectrix::app::kAutoShowcaseDefault != 0;
            repair = true;
        } else {
            status_ = "LOADED";
        }
        if (repair) {
            const esp_err_t save = owner_->storage_->SetUInt32(
                zectrix::app::kAutoShowcaseSettingKey,
                zectrix::app::kAutoShowcaseDefault);
            if (save != ESP_OK) status_ = "DEFAULT NOT SAVED";
        }
        controller_ = zectrix::app::SettingsController(value);
        return context.RequestRender({0, 0, 400, 300},
                                     sdk::RenderIntent::Quality)
                   ? sdk::Status::Ok : sdk::Status::InternalError;
    }

    sdk::Status HandleEvent(const sdk::InputEvent& event,
                          sdk::ApplicationContext& context) override {
        const zectrix::app::SettingsResult result = controller_.Handle(event);
        if (result.decision == zectrix::app::SettingsDecision::RenderFast) {
            status_ = "NOT SAVED";
            context.RequestRender({0, 24, 400, 276},
                                  sdk::RenderIntent::Fast);
        } else if (result.decision == zectrix::app::SettingsDecision::Save) {
            const esp_err_t save = owner_->storage_->SetUInt32(
                zectrix::app::kAutoShowcaseSettingKey,
                result.auto_showcase ? 1 : 0);
            status_ = save == ESP_OK ? "SAVED" : "SAVE FAILED";
            context.RequestRender({0, 24, 400, 276},
                                  sdk::RenderIntent::Fast);
        } else if (result.decision == zectrix::app::SettingsDecision::Back) {
            return owner_->RequestBack(context);
        } else if (result.decision ==
                   zectrix::app::SettingsDecision::Shutdown) {
            context.RequestCommand(sdk::AppCommand::Shutdown());
        }
        return sdk::Status::Ok;
    }

    sdk::Status Render(const sdk::RenderRequest& request) override {
        return ToSdkStatus(owner_->ui_.ShowSettings(
            controller_.auto_showcase(), status_,
            request.intent == sdk::RenderIntent::Quality));
    }

    sdk::Status Exit() override { return sdk::Status::Ok; }

private:
    TerminalApp* owner_;
    zectrix::app::SettingsController controller_;
    const char* status_ = "";
};

sdk::Status TerminalApp::CreateSettings(TerminalApp& owner, sdk::Application** output) {
    return CreateApplication<SettingsApplication>(owner, output);
}

}  // namespace zectrix::terminal
