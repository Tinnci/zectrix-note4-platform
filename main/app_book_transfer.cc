#include "terminal_internal.h"

#include "esp_log.h"

#include "zectrix_book_transfer_controller.h"
#include "zectrix_connectivity_service.h"

namespace zectrix::terminal {

class TerminalApp::BookTransferApplication final : public sdk::Application {
public:
    explicit BookTransferApplication(TerminalApp& owner) : owner_(owner) {}
    sdk::Status Enter(sdk::ApplicationContext& context) override {
        const auto result = controller_.Start();
        if (!sdk::IsOk(result)) return result;
        return Apply(zectrix::app::BookTransferDecision::RenderQuality, context);
    }
    sdk::Status HandleEvent(const sdk::InputEvent& event, sdk::ApplicationContext& context) override {
        return Apply(controller_.Handle(event), context);
    }
    sdk::Status HandleIdle(sdk::ApplicationContext& context) override {
        return Apply(controller_.Update(owner_.connectivity_->BookTransferStatus(), owner_.time_->MonotonicMicroseconds()), context);
    }
    sdk::Status Render(const sdk::RenderRequest& request) override {
        const auto result = owner_.ui_.ShowBookTransfer(controller_.snapshot(),
            controller_.scene() == zectrix::app::BookTransferScene::Mode, controller_.station_selected(),
            request.intent == sdk::RenderIntent::Quality);
        controller_.Presented(result == ESP_OK);
        return ToSdkStatus(result);
    }
    sdk::Status Exit() override {
        controller_.Stop();
        const auto stopped = owner_.connectivity_->StopBookTransfer();
        if (stopped != zectrix::connectivity::ConnectivityResult::kOk)
            ESP_LOGW(kTag, "book transfer stop is retrying on the connectivity owner");
        return sdk::Status::Ok;
    }
private:
    sdk::Status Apply(zectrix::app::BookTransferDecision decision, sdk::ApplicationContext& context) {
        using Decision = zectrix::app::BookTransferDecision;
        using Mode = zectrix::connectivity::BookTransferMode;
        switch (decision) {
            case Decision::Hotspot:
            case Decision::Station:
                owner_.connectivity_->StartBookTransfer(decision == Decision::Hotspot ? Mode::Hotspot : Mode::Station);
                controller_.Update(owner_.connectivity_->BookTransferStatus(), owner_.time_->MonotonicMicroseconds());
                context.RequestRender({0, 24, 400, 276}, sdk::RenderIntent::Quality);
                break;
            case Decision::Stop:
                owner_.connectivity_->StopBookTransfer();
                controller_.Update(owner_.connectivity_->BookTransferStatus(), owner_.time_->MonotonicMicroseconds());
                context.RequestRender({0, 24, 400, 276}, sdk::RenderIntent::Quality);
                break;
            case Decision::RenderFast:
            case Decision::RenderQuality:
                context.RequestRender({0, 24, 400, 276}, decision == Decision::RenderQuality ? sdk::RenderIntent::Quality : sdk::RenderIntent::Fast);
                break;
            case Decision::Reader: {
#if CONFIG_ZECTRIX_ENABLE_READER
                sdk::AppCommand open;
                if (!sdk::AppCommand::Open("reader", &open)) return sdk::Status::InternalError;
                context.RequestCommand(open);
#else
                context.RequestCommand(sdk::AppCommand::Home());
#endif
                break;
            }
            case Decision::Back: return owner_.RequestBack(context);
            case Decision::Shutdown: context.RequestCommand(sdk::AppCommand::Shutdown()); break;
            case Decision::None: break;
        }
        return sdk::Status::Ok;
    }
    TerminalApp& owner_;
    zectrix::app::BookTransferController controller_;
};

sdk::Status TerminalApp::CreateBookTransfer(TerminalApp& owner, sdk::Application** output) {
    return CreateApplication<BookTransferApplication>(owner, output);
}

}  // namespace zectrix::terminal
