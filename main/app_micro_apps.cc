#include "terminal_internal.h"
#include "zectrix_micro_app_controller.h"
#include "zectrix_storage_service.h"
#include "esp_log.h"

namespace zectrix::terminal {

class TerminalApp::MicroAppsApplication final : public sdk::Application {
public:
    explicit MicroAppsApplication(TerminalApp& owner)
        : owner_(owner), library_(*owner.storage_), controller_(library_) {}
    sdk::Status Enter(sdk::ApplicationContext& context) override {
        const auto result = controller_.Start();
        return sdk::IsOk(result) ? Update(controller_.Tick(), context) : result;
    }
    sdk::Status HandleEvent(const sdk::InputEvent& event, sdk::ApplicationContext& context) override {
        return Update(controller_.Handle(event), context);
    }
    sdk::Status HandleIdle(sdk::ApplicationContext& context) override {
        return Update(controller_.Tick(), context);
    }
    sdk::Status Render(const sdk::RenderRequest& request) override {
        const auto result = owner_.ui_.ShowMicroApps(controller_, request.intent == sdk::RenderIntent::Quality);
        controller_.Presented(result == ESP_OK);
        return ToSdkStatus(result);
    }
    sdk::Status Exit() override {
        controller_.Stop();
        owner_.micro_app_busy_ = false;
        return sdk::Status::Ok;
    }

private:
    class Library final : public app::MicroAppLibrary {
    public:
        explicit Library(storage::StorageService& service) : service_(service) {}
        esp_err_t List(storage::BookEntry* entries, std::size_t capacity, std::size_t* count,
                       bool* more, const char* cursor, bool previous) override {
            return service_.ListApps(entries, capacity, count, more, cursor, previous);
        }
        esp_err_t Open(const char* name, storage::BookFile* file) override { return service_.OpenApp(name, file); }
    private:
        storage::StorageService& service_;
    };

    sdk::Status Update(app::MicroAppDecision decision, sdk::ApplicationContext& context) {
        owner_.micro_app_busy_ = controller_.busy();
        if (controller_.scene() == app::MicroAppScene::Error && last_scene_ != app::MicroAppScene::Error) {
            ESP_LOGW(kTag, "app %s stopped: storage=%s runtime=%s peak=%u",
                controller_.name(), esp_err_to_name(controller_.storage_result()), controller_.engine().detail(),
                static_cast<unsigned>(controller_.engine().heap().peak));
        }
        last_scene_ = controller_.scene();
        switch (decision) {
            case app::MicroAppDecision::Back: return owner_.RequestBack(context);
            case app::MicroAppDecision::Shutdown: context.RequestCommand(sdk::AppCommand::Shutdown()); break;
            case app::MicroAppDecision::RenderFast:
            case app::MicroAppDecision::RenderQuality:
                context.RequestRender({0, 24, 400, 276}, decision == app::MicroAppDecision::RenderQuality
                    ? sdk::RenderIntent::Quality : sdk::RenderIntent::Fast);
                break;
            case app::MicroAppDecision::None: break;
        }
        return sdk::Status::Ok;
    }
    TerminalApp& owner_;
    Library library_;
    app::MicroAppController controller_;
    app::MicroAppScene last_scene_ = app::MicroAppScene::List;
};

sdk::Status TerminalApp::CreateMicroApps(TerminalApp& owner, sdk::Application** output) {
    return CreateApplication<MicroAppsApplication>(owner, output);
}

}  // namespace zectrix::terminal
