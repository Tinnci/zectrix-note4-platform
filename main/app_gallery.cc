#include "zectrix_locale.h"
#include "terminal_internal.h"

#include <cstring>
#include <iterator>

#include "terminal_assets.h"
#include "zectrix_gallery_controller.h"

using zectrix::i18n::Tr;
using zectrix::i18n::Text;

namespace zectrix::terminal {

constexpr uint8_t kFootprintMagic[] = {'Z', 'F', 'P', '1'};
constexpr size_t kAnimationHeaderSize = 8;
constexpr size_t kStepHeaderSize = 12;
uint16_t ReadLe16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) | static_cast<uint16_t>(data[1] << 8);
}

class TerminalApp::GalleryApplication : public sdk::Application {
public:
    explicit GalleryApplication(TerminalApp& owner, bool automatic = false)
        : owner_(&owner), controller_(automatic, owner.gallery_selection_) {}

    sdk::Status Enter(sdk::ApplicationContext& context) override {
        const auto result = controller_.Start();
        if (!sdk::IsOk(result)) return result;
        context.RequestRender({0, 24, 400, 276}, sdk::RenderIntent::Quality);
        return sdk::Status::Ok;
    }
    sdk::Status HandleEvent(const sdk::InputEvent& event,
                            sdk::ApplicationContext& context) override {
        Apply(controller_.Handle(event), context);
        return sdk::Status::Ok;
    }
    sdk::Status HandleIdle(sdk::ApplicationContext& context) override {
        Apply(controller_.Tick(owner_->time_->MonotonicMicroseconds()), context);
        return sdk::Status::Ok;
    }
    sdk::Status Render(const sdk::RenderRequest& request) override {
        using zectrix::app::GalleryPage;
        const bool quality = request.intent == sdk::RenderIntent::Quality;
        if (controller_.page() == GalleryPage::Menu) {
            const char* kItems[] = {
                Tr(Text::LighthouseChoice), Tr(Text::FootprintsChoice),
                Tr(Text::MountainChoice), Tr(Text::RunAllScenes)};
            return ToSdkStatus(owner_->ui_.ShowMenu(Tr(Text::DisplayGallery), kItems,
                std::size(kItems), controller_.selected(),
                Tr(Text::NavViewBack), quality));
        }
        const uint32_t image = controller_.image();
        if (controller_.page() == GalleryPage::Report) {
            const char* kTitles[] = {Tr(Text::Lighthouse), Tr(Text::Footprints), Tr(Text::MountainLandscape)};
            const char* kModes[] = {Tr(Text::FullRefresh), Tr(Text::PartialRefresh), Tr(Text::FullPreclear)};
            return ToSdkStatus(owner_->ui_.ShowSceneInfo(kTitles[image], kModes[image],
                image == 2 ? Tr(Text::FormatGray) : Tr(Text::FormatMono),
                image == 2 ? zectrix::display::DisplayService::kFrameBytes4Bpp
                           : zectrix::display::DisplayService::kFrameBytes1Bpp,
                result_.elapsed_ms, result_.error, quality));
        }

        const int64_t started = owner_->time_->MonotonicMicroseconds();
        if (controller_.frame() == 0) {
            result_ = {};
            remaining_steps_ = 0;
            if (image == 0) {
                result_.error = owner_->ui_.ShowImage1Bpp(kLighthouse1bppStart,
                    static_cast<size_t>(kLighthouse1bppEnd - kLighthouse1bppStart));
            } else if (image == 1) {
                result_.error = StartFootprints();
            } else {
                result_.error = owner_->ui_.ShowImage4Bpp(kMountain4bppStart,
                    static_cast<size_t>(kMountain4bppEnd - kMountain4bppStart));
            }
        } else {
            result_.error = DrawFootprint();
        }
        const int64_t completed = owner_->time_->MonotonicMicroseconds();
        result_.elapsed_ms += (completed - started) / 1000;
        const bool more = image == 1 && remaining_steps_ > 0;
        const int64_t hold = image == 2 ? 5000000 : image == 0 ? 2500000 :
            more ? (controller_.frame() == 0 ? 900000 : 400000) : 2200000;
        controller_.Presented(completed, result_.error == ESP_OK, more, hold);
        return ToSdkStatus(result_.error);
    }
    sdk::Status Exit() override {
        owner_->gallery_selection_ = controller_.selected();
        controller_.Stop();
        return sdk::Status::Ok;
    }

private:
    void Apply(zectrix::app::GalleryDecision decision, sdk::ApplicationContext& context) {
        using Decision = zectrix::app::GalleryDecision;
        if (decision == Decision::Shutdown) context.RequestCommand(sdk::AppCommand::Shutdown());
        else if (decision == Decision::Back) owner_->RequestBack(context);
        else if (decision == Decision::RenderFast || decision == Decision::RenderQuality) {
            context.RequestRender({0, 24, 400, 276}, decision == Decision::RenderQuality
                ? sdk::RenderIntent::Quality : sdk::RenderIntent::Fast);
        }
    }
    esp_err_t StartFootprints() {
        const size_t size = static_cast<size_t>(kFootprintAnimationEnd - kFootprintAnimationStart);
        if (size < kAnimationHeaderSize ||
            std::memcmp(kFootprintAnimationStart, kFootprintMagic, sizeof(kFootprintMagic)) != 0)
            return ESP_ERR_INVALID_SIZE;
        remaining_steps_ = ReadLe16(kFootprintAnimationStart + 4);
        animation_cursor_ = kFootprintAnimationStart + kAnimationHeaderSize;
        return owner_->ui_.ShowImage1Bpp(kSnowPath1bppStart,
            static_cast<size_t>(kSnowPath1bppEnd - kSnowPath1bppStart));
    }
    esp_err_t DrawFootprint() {
        if (!remaining_steps_ || !animation_cursor_ ||
            static_cast<size_t>(kFootprintAnimationEnd - animation_cursor_) < kStepHeaderSize)
            return ESP_ERR_INVALID_SIZE;
        const zectrix::display::Rect region = {ReadLe16(animation_cursor_),
            ReadLe16(animation_cursor_ + 2), ReadLe16(animation_cursor_ + 4),
            ReadLe16(animation_cursor_ + 6)};
        const size_t size = ReadLe16(animation_cursor_ + 10);
        animation_cursor_ += kStepHeaderSize;
        if (size > static_cast<size_t>(kFootprintAnimationEnd - animation_cursor_))
            return ESP_ERR_INVALID_SIZE;
        const esp_err_t result = owner_->ui_.ShowImagePatch(region, animation_cursor_, size);
        animation_cursor_ += size;
        --remaining_steps_;
        return result;
    }

    TerminalApp* owner_;
    zectrix::app::GalleryController controller_;
    SceneResult result_{};
    const uint8_t* animation_cursor_ = nullptr;
    uint16_t remaining_steps_ = 0;
};

sdk::Status TerminalApp::CreateGallery(TerminalApp& owner, sdk::Application** output) {
    return CreateApplication<GalleryApplication>(owner, output);
}

class TerminalApp::ShowcaseApplication final : public GalleryApplication {
public:
    explicit ShowcaseApplication(TerminalApp& owner) : GalleryApplication(owner, true) {}
};

sdk::Status TerminalApp::CreateShowcase(TerminalApp& owner, sdk::Application** output) {
    return CreateApplication<ShowcaseApplication>(owner, output);
}

}  // namespace zectrix::terminal
