#include "zectrix_locale.h"
#include "terminal_internal.h"

#include "zectrix_storage_service.h"
#if CONFIG_ZECTRIX_ENABLE_READER
#include "zectrix_reader_platform.h"
#endif


using zectrix::i18n::Tr;
using zectrix::i18n::Text;

namespace zectrix::terminal {

app::ReadingOverview TerminalApp::ReadReadingOverview() {
    app::ReadingOverview overview;
#if CONFIG_ZECTRIX_ENABLE_READER
    if (!storage_) return overview;
    reader::PlatformBookmarkStore store(*storage_);
    reader::Bookmarks bookmarks(store);
    overview.state = app::ReadingOverview::State::Empty;
    if (bookmarks.Load() != reader::Result::Ok) {
        overview.state = app::ReadingOverview::State::Error;
    } else if (const auto* latest = bookmarks.Latest()) {
        overview.state = app::ReadingOverview::State::Saved;
        overview.book_id = latest->book_id;
        overview.progress_per_mille = latest->progress_per_mille;
    }
#endif
    return overview;
}

app::SleepCoverSnapshot TerminalApp::ReadSleepCover() {
    app::SleepCoverSnapshot snapshot;
    snapshot.clock = time_->Now();
    snapshot.power = power_->ReadSnapshot();
    const auto reading = ReadReadingOverview();
    snapshot.reading.book_id = reading.book_id;
    snapshot.reading.progress_per_mille = reading.progress_per_mille;
    snapshot.has_reading = reading.state == app::ReadingOverview::State::Saved;
    return snapshot;
}

class TerminalApp::SleepCoverApplication final : public sdk::Application {
public:
    explicit SleepCoverApplication(TerminalApp& owner) : owner_(owner) {}
    sdk::Status Enter(sdk::ApplicationContext& context) override {
        const auto result = controller_.Start(owner_.sleep_cover_style_);
        return sdk::IsOk(result) ? Apply(zectrix::app::SleepCoverDecision::RenderQuality, context) : result;
    }
    sdk::Status HandleEvent(const sdk::InputEvent& event, sdk::ApplicationContext& context) override {
        return Apply(controller_.Handle(event), context);
    }
    sdk::Status HandleIdle(sdk::ApplicationContext& context) override { return Apply(controller_.Tick(), context); }
    sdk::Status Render(const sdk::RenderRequest& request) override {
        const auto result = controller_.scene() == zectrix::app::SleepCoverScene::Choose
            ? owner_.ui_.ShowSleepCoverMenu(controller_.selected(), owner_.sleep_cover_style_,
                owner_.sleep_cover_saved_ ? nullptr : Tr(Text::CoverSaveRetry),
                request.intent == sdk::RenderIntent::Quality)
            : owner_.ui_.ShowSleepCover(snapshot_, controller_.selected(), true, owner_.sleep_cover_saved_);
        controller_.Presented(result == ESP_OK);
        return ToSdkStatus(result);
    }
    sdk::Status Exit() override { controller_.Stop(); return sdk::Status::Ok; }

private:
    sdk::Status Apply(zectrix::app::SleepCoverDecision decision, sdk::ApplicationContext& context) {
        using Decision = zectrix::app::SleepCoverDecision;
        if (decision == Decision::Choose) {
            if (controller_.selected() != owner_.sleep_cover_style_ || !owner_.sleep_cover_saved_) {
                owner_.sleep_cover_style_ = controller_.selected();
                owner_.sleep_cover_saved_ = owner_.storage_->SetUInt32(zectrix::app::kSleepCoverSettingKey,
                    static_cast<uint32_t>(owner_.sleep_cover_style_)) == ESP_OK;
            }
            snapshot_ = owner_.ReadSleepCover();
            decision = Decision::RenderQuality;
        }
        if (decision == Decision::RenderFast || decision == Decision::RenderQuality)
            context.RequestRender({0, 24, 400, 276}, decision == Decision::RenderQuality
                ? sdk::RenderIntent::Quality : sdk::RenderIntent::Fast);
        else if (decision == Decision::Back) return owner_.RequestBack(context);
        else if (decision == Decision::Shutdown) context.RequestCommand(sdk::AppCommand::Shutdown());
        return sdk::Status::Ok;
    }
    TerminalApp& owner_;
    zectrix::app::SleepCoverController controller_;
    zectrix::app::SleepCoverSnapshot snapshot_{};
};

sdk::Status TerminalApp::CreateSleepCover(TerminalApp& owner, sdk::Application** output) {
    return CreateApplication<SleepCoverApplication>(owner, output);
}

}  // namespace zectrix::terminal
