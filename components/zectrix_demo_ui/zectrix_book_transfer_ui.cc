#include "zectrix_locale.h"
#include "zectrix_demo_ui.h"
#include "zectrix_book_transfer.h"
#include "zectrix_unicode_text.h"
#include "sdkconfig.h"

#include <cstdio>

using zectrix::i18n::Tr;
using zectrix::i18n::Text;

using zectrix::ui::DrawUtf8Line;

esp_err_t ZectrixDemoUi::ShowBookTransfer(const zectrix::connectivity::BookTransferSnapshot& status,
                                        bool choosing_mode, bool station_selected, bool full_refresh) {
    using namespace zectrix::connectivity;
    if (choosing_mode) {
        DrawFrame(Tr(Text::SendBooks), Tr(Text::NavTransferMode));
        const char* choices[] = {Tr(Text::CreateHotspot), Tr(Text::UseHomeWifi)};
        const char* details[] = {Tr(Text::HotspotDetail), Tr(Text::HomeWifiDetail)};
        for (unsigned i = 0; i < 2; ++i) {
            const int y = 70 + i * 76;
            const bool selected = i == (station_selected ? 1u : 0u);
            canvas_.FillRect(16, y, 368, 36, selected);
            canvas_.Rect(16, y, 368, 36);
            canvas_.Text(28, y + 10, choices[i], 1, selected);
            canvas_.Text(16, y + 42, details[i]);
        }
        canvas_.TextCentered(236, Tr(Text::ManageBooks));
        canvas_.TextCentered(254, Tr(Text::WifiOffWhenFinished));
    } else if (status.state == BookTransferState::Complete) {
#if CONFIG_ZECTRIX_ENABLE_READER
        DrawFrame(Tr(Text::TransferFinished), Tr(Text::NavTransferRead));
#else
        DrawFrame(Tr(Text::TransferFinished), Tr(Text::NavTransferHome));
#endif
        canvas_.TextCentered(86, Tr(Text::WifiIsOff), 2);
        char count[48];
        std::snprintf(count, sizeof(count), Tr(Text::BooksAdded), static_cast<unsigned long>(status.uploaded));
        canvas_.TextCentered(144, count);
        canvas_.TextCentered(194, Tr(Text::LibraryUpdated));
    } else if (status.state == BookTransferState::Failed || status.state == BookTransferState::Stopping) {
        DrawFrame(Tr(Text::BookTransfer), status.state == BookTransferState::Stopping ? Tr(Text::NavRetryStop) : Tr(Text::NavTransferBack));
        const char* message = Tr(Text::TransferUnavailable);
        const char* detail = Tr(Text::TryHotspot);
        switch (status.error) {
            case BookTransferError::Storage: message = Tr(Text::BookStorageUnavailable); detail = Tr(Text::InstallLibrary); break;
            case BookTransferError::Wifi: message = Tr(Text::NetworkUnavailable); break;
            case BookTransferError::Server: message = Tr(Text::WebServerUnavailable); break;
            case BookTransferError::Timeout: message = Tr(Text::TransferTimedOut); break;
            case BookTransferError::Power: message = Tr(Text::ChargeBeforeWifi); detail = Tr(Text::ChargeTo20); break;
            case BookTransferError::Policy: message = Tr(Text::WifiDisabledPolicy); detail = Tr(Text::EnableWifiPolicy); break;
            case BookTransferError::Stop: message = Tr(Text::WifiStopRetry); detail = Tr(Text::RetryRadioShutdown); break;
            case BookTransferError::Busy: message = Tr(Text::TransferBusy); detail = Tr(Text::WaitThenRetry); break;
            case BookTransferError::Credentials: message = Tr(Text::NoSavedWifi); detail = Tr(Text::ChooseHotspot); break;
            case BookTransferError::None: message = Tr(Text::StoppingWifi); detail = Tr(Text::CleaningFiles); break;
        }
        canvas_.TextCentered(108, message);
        canvas_.TextCentered(158, detail);
    } else {
        DrawFrame(status.state == BookTransferState::Starting ? Tr(Text::StartingWifi) : Tr(Text::SendBooks), Tr(Text::NavFinishCancel));
        canvas_.Text(16, 56, status.mode == BookTransferMode::Hotspot ? Tr(Text::JoinHotspot) : Tr(Text::JoinNetwork));
        DrawUtf8Line(canvas_, 16, 78, status.ssid.data(), 368);
        canvas_.Text(16, 108, status.mode == BookTransferMode::Hotspot ? Tr(Text::WifiWebCode) : Tr(Text::WebCode));
        canvas_.Text(16, 130, status.code.data(), 2);
        canvas_.Text(16, 173, Tr(Text::OpenBrowser));
        char line[64];
        if (status.address[0]) std::snprintf(line, sizeof(line), "http://%s/", status.address.data());
        else std::snprintf(line, sizeof(line), "%s", Tr(Text::WaitingAddress));
        canvas_.Text(16, 194, line);
        const unsigned percent = status.expected ? static_cast<uint64_t>(status.received) * 100 / status.expected : 0;
        std::snprintf(line, sizeof(line), Tr(Text::UploadProgress), static_cast<unsigned long>(status.uploaded), percent);
        canvas_.Text(16, 234, line);
        canvas_.Text(16, 252, Tr(Text::KeepTransferOpen));
    }
    return full_refresh ? RefreshFull() : RefreshAuto();
}
