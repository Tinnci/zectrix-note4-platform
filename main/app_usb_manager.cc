#include "terminal_internal.h"

#include "zectrix_usb_manager.h"
#include "zectrix_storage_service.h"

namespace zectrix::terminal {

class TerminalApp::UsbManagerApplication final : public sdk::Application {
public:
    explicit UsbManagerApplication(TerminalApp& owner)
        : owner_(owner), settings_(*owner.storage_, owner.language_saved_, owner.sleep_cover_style_, owner.sleep_cover_saved_),
          session_(*owner.usb_host_, settings_) {}

    sdk::Status Enter(sdk::ApplicationContext& context) override {
        Start();
        return Update(context);
    }
    sdk::Status HandleEvent(const sdk::InputEvent& event, sdk::ApplicationContext& context) override {
        switch (controller_.Handle(event)) {
            case app::UsbDecision::Back: return owner_.RequestBack(context);
            case app::UsbDecision::Shutdown: context.RequestCommand(sdk::AppCommand::Shutdown()); break;
            case app::UsbDecision::Cancel: session_.Cancel(); break;
            case app::UsbDecision::Retry: Start(); break;
            default: break;
        }
        return Update(context);
    }
    sdk::Status HandleIdle(sdk::ApplicationContext& context) override {
        session_.Poll();
        return Update(context);
    }
    sdk::Status Render(const sdk::RenderRequest& request) override {
        const auto result = owner_.ui_.ShowUsbManager(controller_.snapshot(), controller_.storage_ready(),
            request.intent == sdk::RenderIntent::Quality);
        controller_.Presented(result == ESP_OK);
        return ToSdkStatus(result);
    }
    sdk::Status Exit() override { session_.Stop(); return sdk::Status::Ok; }

private:
    void Start() {
        storage::BookStorage* books = nullptr;
        storage_result_ = owner_.storage_->BeginBookManagement(&books);
        const bool ready = storage_result_ == ESP_OK;
        if (ready) session_.Start(*books);
        controller_.Start(ready);
    }
    sdk::Status Update(sdk::ApplicationContext& context) {
        auto snapshot = session_.snapshot();
        if (!controller_.storage_ready()) snapshot.error = storage_result_ == ESP_ERR_INVALID_STATE
            ? host::Status::Busy : host::Status::Unavailable;
        const auto decision = controller_.Update(snapshot, owner_.time_->MonotonicMicroseconds());
        if (decision == app::UsbDecision::RenderFast || decision == app::UsbDecision::RenderQuality) {
            context.RequestRender({0, 0, 400, 300}, decision == app::UsbDecision::RenderQuality
                ? sdk::RenderIntent::Quality : sdk::RenderIntent::Fast);
        }
        return sdk::Status::Ok;
    }
    TerminalApp& owner_;
    app::UsbSettings settings_;
    host::BookSession session_;
    app::UsbManagerController controller_;
    esp_err_t storage_result_ = ESP_OK;
};

sdk::Status TerminalApp::CreateUsbManager(TerminalApp& owner, sdk::Application** output) {
    return CreateApplication<UsbManagerApplication>(owner, output);
}

}  // namespace zectrix::terminal
