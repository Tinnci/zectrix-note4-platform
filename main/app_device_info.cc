#include "terminal_internal.h"

#include "zectrix_navigation.h"


namespace zectrix::terminal {

class TerminalApp::DeviceInfoApplication : public sdk::Application {
public:
    explicit DeviceInfoApplication(TerminalApp& owner, bool about = false)
        : owner_(&owner), about_(about) {}
    sdk::Status Enter(sdk::ApplicationContext& context) override {
        if (!about_) {
            const esp_err_t read = owner_->system_->ReadSnapshot(&system_);
            if (read != ESP_OK) return ToSdkStatus(read);
            power_ = owner_->power_snapshot_;
        }
        context.RequestRender({0, 24, 400, 276}, sdk::RenderIntent::Quality);
        return sdk::Status::Ok;
    }
    sdk::Status HandleEvent(const sdk::InputEvent& event,
                            sdk::ApplicationContext& context) override {
        const auto key = app::MapNavigation(event);
        if (key == app::Navigation::Back) return owner_->RequestBack(context);
        if (key == app::Navigation::Shutdown) context.RequestCommand(sdk::AppCommand::Shutdown());
        return sdk::Status::Ok;
    }
    sdk::Status HandleIdle(sdk::ApplicationContext& context) override {
        const auto& current = owner_->power_snapshot_;
        if (!about_ && (current.battery_valid != power_.battery_valid ||
            current.battery_percent != power_.battery_percent || current.battery_mv != power_.battery_mv ||
            current.external_power_present != power_.external_power_present || current.charging != power_.charging)) {
            power_ = current;
            context.RequestRender({0, 24, 400, 276}, sdk::RenderIntent::Fast);
        }
        return sdk::Status::Ok;
    }
    sdk::Status Render(const sdk::RenderRequest& request) override {
        const bool quality = request.intent == sdk::RenderIntent::Quality;
        return ToSdkStatus(about_ ? owner_->ui_.ShowAbout(quality)
            : owner_->ui_.ShowDeviceInfo(power_, system_, quality));
    }
    sdk::Status Exit() override { return sdk::Status::Ok; }

private:
    TerminalApp* owner_;
    bool about_;
    zectrix::power::PowerSnapshot power_{};
    zectrix::system::SystemSnapshot system_{};
};

sdk::Status TerminalApp::CreateDeviceInfo(TerminalApp& owner, sdk::Application** output) {
    return CreateApplication<DeviceInfoApplication>(owner, output);
}

class TerminalApp::AboutApplication final : public DeviceInfoApplication {
public:
    explicit AboutApplication(TerminalApp& owner) : DeviceInfoApplication(owner, true) {}
};

sdk::Status TerminalApp::CreateAbout(TerminalApp& owner, sdk::Application** output) {
    return CreateApplication<AboutApplication>(owner, output);
}

}  // namespace zectrix::terminal
