#include "zectrix_demo_ui.h"
#include "zectrix_locale.h"
#include "zectrix_micro_app_controller.h"
#include "zectrix_micro_app_view.h"
#include "zectrix_unicode_text.h"
#include "sdkconfig.h"

#include <cstdio>

esp_err_t ZectrixDemoUi::ShowMicroApps(const zectrix::app::MicroAppController& apps, bool full_refresh) {
    using namespace zectrix;
    using i18n::Text;
    using i18n::Tr;
    using app::MicroAppScene;
    const auto scene = apps.scene();
    DrawFrame(scene == MicroAppScene::List ? Tr(Text::Apps) : apps.title(),
              Tr(scene == MicroAppScene::List ? Text::NavOpenBack :
                 scene == MicroAppScene::Error ? Text::NavAppsReturn : Text::NavBackOff));
    if (scene == MicroAppScene::List) {
        if (!apps.count()) {
            canvas_.TextCentered(100, Tr(apps.storage_result() == ESP_OK ? Text::NoApps : Text::AppStorageUnavailable));
#if CONFIG_ZECTRIX_ENABLE_USB_HOST || CONFIG_ZECTRIX_ENABLE_BOOK_TRANSFER
            canvas_.TextCentered(146, Tr(Text::AppsInstallHint));
#else
            canvas_.TextCentered(146, Tr(Text::AppsNoTransfer));
#endif
            canvas_.TextCentered(192, Tr(Text::NavRetryLibrary));
        } else {
            char page[48];
            std::snprintf(page, sizeof(page), Tr(Text::AppsPage), apps.page() + 1);
            canvas_.Text(16, 46, page);
            if (apps.selected() < apps.count()) {
                if (const auto* meta = apps.metadata(apps.selected()))
                    ui::DrawUtf8Line(canvas_, 232, 46, meta->author.data(), 152);
            }
            for (std::size_t i = 0; i < apps.rows(); ++i) {
                const int y = 70 + i * 27;
                const bool selected = i == apps.selected();
                canvas_.FillRect(12, y, 376, 24, selected);
                if (!selected) canvas_.Rect(12, y, 376, 24);
                const auto* meta = i < apps.count() ? apps.metadata(i) : nullptr;
                const char* label = meta ? meta->name.data() : i < apps.count() ? apps.entry(i).name.data() :
                    Tr(apps.previous() && i == apps.count() ? Text::AppsPrevious : Text::AppsNext);
                if (meta) {
                    ui::DrawMicroAppIcon(canvas_, *meta, 20, y + 4, 16, selected);
                    ui::DrawUtf8Line(canvas_, 44, y + 4, label, 240, selected);
                    ui::DrawUtf8Line(canvas_, 292, y + 4, meta->version.data(), 88, selected);
                } else {
                    ui::DrawUtf8Line(canvas_, 20, y + 4, label, 360, selected);
                }
            }
        }
    } else if (scene == MicroAppScene::Loading) {
        const auto& meta = apps.current_metadata();
        if (meta.icon_side) {
            ui::DrawMicroAppIcon(canvas_, meta, 184, 80, 32);
            ui::DrawUtf8Line(canvas_, 20, 164, meta.author.data(), 360);
            ui::DrawUtf8Line(canvas_, 20, 190, meta.version.data(), 360);
        }
        canvas_.TextCentered(122, Tr(Text::AppLoading));
    } else if (scene == MicroAppScene::Running) {
        ui::DrawMicroAppFrame(canvas_, apps.engine().frame());
    } else {
        Text reason = Text::AppFailed;
        if (apps.storage_result() == ESP_ERR_INVALID_ARG || apps.storage_result() == ESP_ERR_INVALID_SIZE)
            reason = Text::AppInvalidSource;
        else if (apps.storage_result() != ESP_OK) reason = Text::AppReadFailed;
        else if (apps.engine().error() == runtime::Error::Instructions) reason = Text::AppInstructionLimit;
        else if (apps.engine().error() == runtime::Error::Memory) reason = Text::AppMemoryLimit;
        else if (apps.engine().error() == runtime::Error::InvalidSource) reason = Text::AppInvalidSource;
        else if (apps.engine().error() == runtime::Error::Permission) reason = Text::AppPermissionDenied;
        canvas_.TextCentered(100, Tr(reason));
        canvas_.TextCentered(158, Tr(Text::AppTryAnother));
    }
    return full_refresh ? RefreshFull() : RefreshAuto();
}
