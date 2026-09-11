#include "terminal_internal.h"

#include "zectrix_first_party_app_controllers.h"
#include "zectrix_language_setting.h"
#include "zectrix_storage_service.h"

namespace zectrix::terminal {

class TerminalApp::SettingsApplication final : public sdk::Application {
public:
    explicit SettingsApplication(TerminalApp& owner)
        : owner_(&owner), controller_(app::kAutoShowcaseDefault != 0) {}

    sdk::Status Enter(sdk::ApplicationContext& context) override {
        owner_->BindScenes(controller_);
        uint32_t stored = app::kAutoShowcaseDefault;
        const auto read = owner_->storage_->GetUInt32(app::kAutoShowcaseSettingKey, &stored);
        bool value = app::kAutoShowcaseDefault != 0;
        bool repair = false;
        if (read == ESP_ERR_NOT_FOUND) {
            status_ = i18n::Text::DefaultCreated;
            repair = true;
        } else if (read != ESP_OK) {
            status_ = i18n::Text::LoadFailedDefault;
        } else if (!app::NormalizeAutoShowcaseSetting(stored, &value)) {
            status_ = i18n::Text::InvalidReset;
            repair = true;
        } else {
            status_ = i18n::Text::Loaded;
        }
        if (repair && owner_->storage_->SetUInt32(app::kAutoShowcaseSettingKey,
                                                  app::kAutoShowcaseDefault) != ESP_OK)
            status_ = i18n::Text::DefaultNotSaved;
        if (!owner_->language_saved_ && i18n::LanguageCount() > 1) status_ = i18n::Text::LanguageSaveFailed;
        const auto started = controller_.Start(value, i18n::CurrentLanguage());
        return sdk::IsOk(started) ? Apply(controller_.Tick(), context) : started;
    }

    sdk::Status HandleEvent(const sdk::InputEvent& event, sdk::ApplicationContext& context) override {
        return Apply(controller_.Handle(event), context);
    }
    sdk::Status HandleIdle(sdk::ApplicationContext& context) override {
        return Apply(controller_.Tick(), context);
    }
    sdk::Status Render(const sdk::RenderRequest& request) override {
        const auto result = owner_->ui_.ShowSettings(controller_, i18n::Tr(status_),
            request.intent == sdk::RenderIntent::Quality);
        controller_.Presented(result == ESP_OK);
        return ToSdkStatus(result);
    }
    sdk::Status Exit() override { controller_.Stop(); return sdk::Status::Ok; }

private:
    sdk::Status Apply(app::SettingsResult result, sdk::ApplicationContext& context) {
        using Decision = app::SettingsDecision;
        if (result.decision == Decision::None) return sdk::Status::Ok;
        if (result.decision == Decision::Back) return owner_->RequestBack(context);
        if (result.decision == Decision::Shutdown) {
            context.RequestCommand(sdk::AppCommand::Shutdown());
            return sdk::Status::Ok;
        }
        if (result.decision == Decision::SaveLanguage) {
            owner_->language_saved_ = i18n::SaveLanguage(*owner_->storage_, result.language) == ESP_OK;
            status_ = owner_->language_saved_ ? i18n::Text::Saved : i18n::Text::LanguageSaveFailed;
        } else if (result.decision == Decision::Save) {
            const auto saved = owner_->storage_->SetUInt32(app::kAutoShowcaseSettingKey,
                                                           result.auto_showcase ? 1 : 0);
            controller_.SaveCompleted(saved == ESP_OK);
            status_ = saved == ESP_OK ? i18n::Text::Saved : i18n::Text::SaveFailed;
        }
        const bool quality = result.decision == Decision::RenderQuality || result.decision == Decision::SaveLanguage;
        return context.RequestRender({0, 0, 400, 300}, quality ? sdk::RenderIntent::Quality : sdk::RenderIntent::Fast)
            ? sdk::Status::Ok : sdk::Status::InternalError;
    }

    TerminalApp* owner_;
    app::SettingsController controller_;
    i18n::Text status_ = i18n::Text::None;
};

sdk::Status TerminalApp::CreateSettings(TerminalApp& owner, sdk::Application** output) {
    return CreateApplication<SettingsApplication>(owner, output);
}

}  // namespace zectrix::terminal
