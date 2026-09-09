#include "terminal_internal.h"

#include "esp_log.h"

#include "zectrix_reader_controller.h"
#include "zectrix_reader_platform.h"

namespace zectrix::terminal {

class TerminalApp::ReaderApplication final : public sdk::Application {
public:
    explicit ReaderApplication(TerminalApp& owner)
        : owner_(&owner), library_(*owner.storage_),
          store_(*owner.storage_, owner.connectivity_), bookmarks_(store_),
          controller_(library_, bookmarks_, owner.reader_selection_) {}

    sdk::Status Enter(sdk::ApplicationContext& context) override {
        const auto result = controller_.Start();
        if (!sdk::IsOk(result)) return result;
        owner_->LogHeap("reader active");
        return context.RequestRender({0, 24, 400, 276}, sdk::RenderIntent::Quality)
            ? sdk::Status::Ok : sdk::Status::InternalError;
    }
    sdk::Status HandleEvent(const sdk::InputEvent& event, sdk::ApplicationContext& context) override {
        return Apply(controller_.Handle(event), context);
    }
    sdk::Status HandleIdle(sdk::ApplicationContext& context) override {
        return Apply(controller_.Tick(owner_->time_->MonotonicMicroseconds()), context);
    }
    sdk::Status Render(const sdk::RenderRequest& request) override {
        const auto result = owner_->ui_.ShowReader(controller_, request.intent == sdk::RenderIntent::Quality);
        controller_.Presented(result == ESP_OK);
        return ToSdkStatus(result);
    }
    sdk::Status Exit() override {
        owner_->reader_selection_ = controller_.selected();
        owner_->reader_busy_ = false;
        controller_.Stop();
        if (controller_.save_result() != zectrix::reader::Result::Ok)
            ESP_LOGW(kTag, "reader progress save failed: %s", zectrix::reader::ResultName(controller_.save_result()));
        return sdk::Status::Ok;
    }

private:
    sdk::Status Apply(zectrix::app::ReaderDecision decision, sdk::ApplicationContext& context) {
        using Decision = zectrix::app::ReaderDecision;
        owner_->reader_busy_ = controller_.busy();
        switch (decision) {
            case Decision::RenderFast:
            case Decision::RenderQuality:
                context.RequestRender({0, 24, 400, 276}, decision == Decision::RenderQuality
                    ? sdk::RenderIntent::Quality : sdk::RenderIntent::Fast);
                break;
            case Decision::Home: context.RequestCommand(sdk::AppCommand::Home()); break;
            case Decision::Shutdown: context.RequestCommand(sdk::AppCommand::Shutdown()); break;
            case Decision::None: break;
        }
        return sdk::Status::Ok;
    }
    TerminalApp* owner_;
    zectrix::reader::StorageLibrary library_;
    zectrix::reader::PlatformBookmarkStore store_;
    zectrix::reader::Bookmarks bookmarks_;
    zectrix::app::ReaderController controller_;
};

sdk::Status TerminalApp::CreateReader(TerminalApp& owner, sdk::Application** output) {
    return CreateApplication<ReaderApplication>(owner, output);
}

}  // namespace zectrix::terminal
