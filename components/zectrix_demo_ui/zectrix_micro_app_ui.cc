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
    DrawFrame(scene == MicroAppScene::List ? Tr(Text::Apps) : apps.name(),
              Tr(scene == MicroAppScene::List ? Text::NavOpenBack :
                 scene == MicroAppScene::Error ? Text::NavAppsReturn : Text::NavBackOff));
    if (scene == MicroAppScene::List) {
        if (!apps.count()) {
            canvas_.TextCentered(100, Tr(apps.storage_result() == ESP_OK ? Text::NoApps : Text::AppStorageUnavailable));
#if CONFIG_ZECTRIX_ENABLE_USB_HOST
            canvas_.TextCentered(146, Tr(Text::AppsInstallHint));
#else
            canvas_.TextCentered(146, Tr(Text::AppsNoTransfer));
#endif
            canvas_.TextCentered(192, Tr(Text::NavRetryLibrary));
        } else {
            char page[48];
            std::snprintf(page, sizeof(page), Tr(Text::AppsPage), apps.page() + 1);
            canvas_.Text(16, 46, page);
            for (std::size_t i = 0; i < apps.rows(); ++i) {
                const int y = 70 + i * 27;
                const bool selected = i == apps.selected();
                canvas_.FillRect(12, y, 376, 24, selected);
                if (!selected) canvas_.Rect(12, y, 376, 24);
                const char* label = i < apps.count() ? apps.entry(i).name.data() :
                    Tr(apps.previous() && i == apps.count() ? Text::AppsPrevious : Text::AppsNext);
                ui::DrawUtf8Line(canvas_, 20, y + 4, label, 360, selected);
            }
        }
    } else if (scene == MicroAppScene::Loading) {
        canvas_.TextCentered(122, Tr(Text::AppLoading));
    } else if (scene == MicroAppScene::Running) {
        ui::DrawMicroAppFrame(canvas_, apps.engine().frame());
    } else {
        Text reason = Text::AppFailed;
        if (apps.storage_result() != ESP_OK) reason = Text::AppReadFailed;
        else if (apps.engine().error() == runtime::Error::Instructions) reason = Text::AppInstructionLimit;
        else if (apps.engine().error() == runtime::Error::Memory) reason = Text::AppMemoryLimit;
        else if (apps.engine().error() == runtime::Error::InvalidSource) reason = Text::AppInvalidSource;
        canvas_.TextCentered(100, Tr(reason));
        canvas_.TextCentered(158, Tr(Text::AppTryAnother));
    }
    return full_refresh ? RefreshFull() : RefreshAuto();
}
