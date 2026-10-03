#include "note4_locale.h"
#include "ui_engine.h"
#include "note4_book_transfer.h"
#include "unicode_text.h"
#include "sdkconfig.h"

#include <cstdio>

using note4::i18n::Tr;
using note4::i18n::Text;

using note4::ui::DrawUtf8Line;

esp_err_t UiEngine::ShowBookTransfer(const note4::connectivity::BookTransferSnapshot& status,
                                        bool choosing_mode, bool station_selected, bool full_refresh) {
    using namespace note4::connectivity;
    if (choosing_mode) {
        auto page = EnterPage(note4::ui::PageSpec().Title(Tr(Text::SendBooks)).Footer(Tr(Text::NavTransferMode)));
        const bool portrait = page.portrait();
        const int width = page.width();
        const int column = width - 32;
        const char* choices[] = {Tr(Text::CreateHotspot), Tr(Text::UseHomeWifi)};
        const char* details[] = {Tr(Text::HotspotDetail), Tr(Text::HomeWifiDetail)};
        for (unsigned i = 0; i < 2; ++i) {
            const int y = 70 + i * (portrait ? 100 : 76);
            const bool selected = i == (station_selected ? 1u : 0u);
            canvas_.FillRect(16, y, column, 36, selected);
            canvas_.Rect(16, y, column, 36);
            canvas_.TextFitted(28, y + 10, choices[i], column - 24, selected);
            WrapText(16, y + 42, details[i], column, 18, portrait ? 2 : 1);
        }
        if (portrait) {
            WrapText(16, 282, Tr(Text::ManageBooks), column, 18, 2, true);
            WrapText(16, 318, Tr(Text::WifiOffWhenFinished), column, 18, 2, true);
        } else {
            canvas_.TextCentered(236, Tr(Text::ManageBooks));
            canvas_.TextCentered(254, Tr(Text::WifiOffWhenFinished));
        }
        return page.Commit(full_refresh);
    } else if (status.state == BookTransferState::Complete) {
#if CONFIG_NOTE4_ENABLE_READER
        auto page = EnterPage(note4::ui::PageSpec().Title(Tr(Text::TransferFinished)).Footer(Tr(Text::NavTransferRead)));
#else
        auto page = EnterPage(note4::ui::PageSpec().Title(Tr(Text::TransferFinished)).Footer(Tr(Text::NavTransferHome)));
#endif
        const bool portrait = page.portrait();
        const int dy = page.dy();
        const int column = page.width() - 32;
        if (portrait) WrapText(16, 86 + dy, Tr(Text::WifiIsOff), column, 18, 2, true);
        else canvas_.TextCentered(86, Tr(Text::WifiIsOff), 2);
        char count[48];
        std::snprintf(count, sizeof(count), Tr(Text::BooksAdded), static_cast<unsigned long>(status.uploaded));
        WrapText(16, 144 + dy, count, column, 18, 2, true);
        WrapText(16, 194 + dy, Tr(Text::LibraryUpdated), column, 18, 2, true);
        return page.Commit(full_refresh);
    } else if (status.state == BookTransferState::Failed || status.state == BookTransferState::Stopping) {
        auto page = EnterPage(note4::ui::PageSpec()
            .Title(Tr(Text::BookTransfer))
            .Footer(status.state == BookTransferState::Stopping ? Tr(Text::NavRetryStop) : Tr(Text::NavTransferBack)));
        const bool portrait = page.portrait();
        const int dy = page.dy();
        const int column = page.width() - 32;
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
        const int lines = WrapText(16, 108 + dy, message, column, 18, 3, true);
        WrapText(16, 158 + dy + (portrait ? (lines - 1) * 18 : 0), detail, column, 18, 3, true);
        return page.Commit(full_refresh);
    } else {
        auto page = EnterPage(note4::ui::PageSpec()
            .Title(status.state == BookTransferState::Starting ? Tr(Text::StartingWifi) : Tr(Text::SendBooks))
            .Footer(Tr(Text::NavFinishCancel)));
        const bool portrait = page.portrait();
        const int column = page.width() - 32;
        if (portrait) {
            int y = 56;
            y += WrapText(16, y, status.mode == BookTransferMode::Hotspot ? Tr(Text::JoinHotspot) : Tr(Text::JoinNetwork), column, 18, 2) * 18 + 4;
            DrawUtf8Line(canvas_, 16, y, status.ssid.data(), column);
            y += 34;
            y += WrapText(16, y, status.mode == BookTransferMode::Hotspot ? Tr(Text::WifiWebCode) : Tr(Text::WebCode), column, 18, 2) * 18 + 4;
            canvas_.Text(16, y, status.code.data(), 2);
            y += 48;
            y += WrapText(16, y, Tr(Text::OpenBrowser), column, 18, 2) * 18 + 4;
            char line[64];
            if (status.address[0]) std::snprintf(line, sizeof(line), "http://%s/", status.address.data());
            else std::snprintf(line, sizeof(line), "%s", Tr(Text::WaitingAddress));
            canvas_.TextFitted(16, y, line, column);
            y += 40;
            const unsigned percent = status.expected ? static_cast<uint64_t>(status.received) * 100 / status.expected : 0;
            std::snprintf(line, sizeof(line), Tr(Text::UploadProgress), static_cast<unsigned long>(status.uploaded), percent);
            canvas_.TextFitted(16, y, line, column);
            WrapText(16, y + 22, Tr(Text::KeepTransferOpen), column, 18, 3);
            return page.Commit(full_refresh);
        }
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
        return page.Commit(full_refresh);
    }
}
