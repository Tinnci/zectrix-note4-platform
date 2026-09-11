#include "zectrix_demo_ui.h"
#include "zectrix_host_books.h"
#include "zectrix_locale.h"
#include "zectrix_unicode_text.h"
#include "sdkconfig.h"

#include <cstdio>

esp_err_t ZectrixDemoUi::ShowUsbManager(const zectrix::host::Snapshot& status, bool storage_ready, bool full_refresh) {
    using namespace zectrix;
    using i18n::Text;
    using i18n::Tr;
    using host::TransferState;
    DrawFrame(Tr(Text::UsbManager), Tr(storage_ready ? Text::NavUsbCancel : Text::NavRetryLibrary));
    if (!storage_ready) {
        canvas_.TextCentered(112, Tr(Text::BookStorageUnavailable));
        canvas_.TextCentered(162, Tr(status.error == host::Status::Busy ? Text::UsbStorageBusy : Text::InstallLibrary));
    } else {
        Text state = Text::UsbWaiting;
        switch (status.state) {
            case TransferState::Waiting: break;
            case TransferState::Ready: state = Text::UsbConnected; break;
            case TransferState::Uploading: state = Text::UsbUploading; break;
            case TransferState::Downloading: state = Text::UsbDownloading; break;
            case TransferState::Complete: state = Text::TransferFinished; break;
            case TransferState::Cancelled: state = Text::UsbCancelled; break;
            case TransferState::Failed: state = Text::UsbTransferFailed; break;
            case TransferState::Removed: state = Text::UsbRemoved; break;
        }
        canvas_.TextCentered(70, Tr(state));
        if (status.state == TransferState::Uploading || status.state == TransferState::Downloading ||
            status.state == TransferState::Complete || status.state == TransferState::Failed) {
            ui::DrawUtf8Line(canvas_, 20, 112, status.name.data(), 360);
            const unsigned percent = status.expected ? static_cast<uint64_t>(status.transferred) * 100 / status.expected : 100;
            canvas_.Rect(20, 144, 360, 14);
            canvas_.FillRect(23, 147, 354 * percent / 100, 8, true);
            char progress[64];
            std::snprintf(progress, sizeof(progress), "%u%%   %lu / %lu B", percent,
                static_cast<unsigned long>(status.transferred), static_cast<unsigned long>(status.expected));
            canvas_.TextCentered(174, progress);
        } else if (status.state == TransferState::Removed) {
            ui::DrawUtf8Line(canvas_, 20, 112, status.name.data(), 360);
        } else {
            canvas_.TextCentered(118, Tr(Text::UsbToolHint));
#if CONFIG_ZECTRIX_ENABLE_RUNTIME
            canvas_.TextCentered(150, Tr(Text::UsbFilesHint));
#else
            canvas_.TextCentered(150, Tr(Text::UsbBooksHint));
#endif
        }
        Text detail = Text::UsbKeepOpen;
        if (status.error == host::Status::NotSaved) detail = Text::UsbNotSaved;
        else if (status.error == host::Status::Exists) detail = Text::UsbNameExists;
        else if (status.error == host::Status::NoSpace) detail = Text::UsbNoSpace;
        else if (status.error != host::Status::Ok && status.error != host::Status::Cancelled) detail = Text::UsbRequestFailed;
        else if (status.settings_revision) detail = Text::UsbSettingsUpdated;
        canvas_.TextCentered(221, Tr(detail));
        char count[64];
        std::snprintf(count, sizeof(count), Tr(Text::UsbFilesAdded), static_cast<unsigned long>(status.uploaded));
        canvas_.TextCentered(250, count);
    }
    return full_refresh ? RefreshFull() : RefreshAuto();
}
