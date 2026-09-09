#include "terminal_internal.h"

#include "esp_log.h"
#include "zectrix_first_party_app_controllers.h"
#include "zectrix_storage_service.h"


namespace zectrix::terminal {

constexpr int64_t kHomeIdleTimeoutUs = 15000000;

class TerminalApp::LauncherApplication final : public sdk::Application {
public:
    explicit LauncherApplication(TerminalApp& owner)
        : owner_(&owner), controller_(owner.applications_.menu_size(), owner.launcher_selection_) {}

    sdk::Status Enter(sdk::ApplicationContext& context) override {
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
        last_input_us_ = owner_->time_->MonotonicMicroseconds();
        return context.RequestRender({0, 0, 400, 300},
                                     sdk::RenderIntent::Quality)
                   ? sdk::Status::Ok : sdk::Status::InternalError;
    }
    sdk::Status HandleEvent(const sdk::InputEvent& event,
                          sdk::ApplicationContext& context) override {
        last_input_us_ = owner_->time_->MonotonicMicroseconds();
        const zectrix::app::LauncherResult result = controller_.Handle(event);
        using Decision = zectrix::app::LauncherDecision;
        const char* target = nullptr;
        switch (result.decision) {
            case Decision::RenderFast:
                context.RequestRender({0, 24, 400, 276}, sdk::RenderIntent::Fast);
                break;
            case Decision::OpenSelected:
                if (const auto* entry = owner_->applications_.MenuAt(result.selected)) target = entry->id;
                break;
            case Decision::Shutdown:
                context.RequestCommand(sdk::AppCommand::Shutdown());
                break;
            case Decision::None: break;
        }
        if (target) {
            sdk::AppCommand open;
            if (!sdk::AppCommand::Open(target, &open)) return sdk::Status::InvalidState;
            context.RequestCommand(open);
        }
        return sdk::Status::Ok;
    }
    sdk::Status HandleIdle(sdk::ApplicationContext& context) override {
        const int64_t now = owner_->time_->MonotonicMicroseconds();
        if (auto_showcase_ && now - last_input_us_ >= kHomeIdleTimeoutUs) {
            last_input_us_ = now;
            sdk::AppCommand open;
            if (sdk::AppCommand::Open("showcase", &open)) context.RequestCommand(open);
        }
        return sdk::Status::Ok;
    }
    sdk::Status Render(const sdk::RenderRequest& request) override {
        std::array<const char*, app::ApplicationCatalog::kCapacity> labels{};
        const auto count = owner_->applications_.menu_size();
        for (std::size_t i = 0; i < count; ++i) labels[i] = owner_->applications_.MenuAt(i)->display_name;
        return ToSdkStatus(owner_->ui_.ShowMenu(
            "ZECTRIX | LAUNCHER", labels.data(), count,
            controller_.selected(),
            "UP/DOWN Move  OK Select  Hold DOWN Off",
            request.intent == sdk::RenderIntent::Quality));
    }
    sdk::Status Exit() override {
        owner_->launcher_selection_ = controller_.selected();
        return sdk::Status::Ok;
    }

private:
    TerminalApp* owner_;
    zectrix::app::LauncherController controller_;
    bool auto_showcase_ = false;
    int64_t last_input_us_ = 0;
};

sdk::Status TerminalApp::CreateLauncher(TerminalApp& owner, sdk::Application** output) {
    return CreateApplication<LauncherApplication>(owner, output);
}

}  // namespace zectrix::terminal
