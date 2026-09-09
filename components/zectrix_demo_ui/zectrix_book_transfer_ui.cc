#include "zectrix_demo_ui.h"
#include "zectrix_book_transfer.h"
#include "zectrix_unicode_text.h"
#include "sdkconfig.h"

#include <cstdio>

using zectrix::ui::DrawUtf8Line;

esp_err_t ZectrixDemoUi::ShowBookTransfer(const zectrix::connectivity::BookTransferSnapshot& status,
                                        bool choosing_mode, bool station_selected, bool full_refresh) {
    using namespace zectrix::connectivity;
    if (choosing_mode) {
        DrawFrame("SEND BOOKS", "UP/DOWN Mode  OK Start  Hold OK Home");
        const char* choices[] = {"CREATE NOTE4 HOTSPOT", "USE SAVED HOME WI-FI"};
        const char* details[] = {"Connect your phone or computer directly", "Use the same network as your computer"};
        for (unsigned i = 0; i < 2; ++i) {
            const int y = 70 + i * 76;
            const bool selected = i == (station_selected ? 1u : 0u);
            canvas_.FillRect(16, y, 368, 36, selected);
            canvas_.Rect(16, y, 368, 36);
            canvas_.Text(28, y + 10, choices[i], 1, selected);
            canvas_.Text(16, y + 42, details[i]);
        }
        canvas_.TextCentered(236, "UPLOAD / DOWNLOAD / MANAGE BOOKS");
        canvas_.TextCentered(254, "Wi-Fi turns off when you finish");
    } else if (status.state == BookTransferState::Complete) {
#if CONFIG_ZECTRIX_ENABLE_READER
        DrawFrame("TRANSFER FINISHED", "OK Read books  Hold OK Transfer menu");
#else
        DrawFrame("TRANSFER FINISHED", "OK Home  Hold OK Transfer menu");
#endif
        canvas_.TextCentered(86, "WI-FI IS OFF", 2);
        char count[48];
        std::snprintf(count, sizeof(count), "%lu BOOKS ADDED", static_cast<unsigned long>(status.uploaded));
        canvas_.TextCentered(144, count);
        canvas_.TextCentered(194, "Your library has been updated.");
    } else if (status.state == BookTransferState::Failed || status.state == BookTransferState::Stopping) {
        DrawFrame("BOOK TRANSFER", status.state == BookTransferState::Stopping ? "OK Retry stop  Hold OK Back" : "OK Transfer menu  Hold OK Back");
        const char* message = "TRANSFER UNAVAILABLE";
        const char* detail = "Try again or use hotspot mode.";
        switch (status.error) {
            case BookTransferError::Storage: message = "BOOK STORAGE UNAVAILABLE"; detail = "Install the starter library via USB."; break;
            case BookTransferError::Wifi: message = "NETWORK UNAVAILABLE"; break;
            case BookTransferError::Server: message = "WEB SERVER UNAVAILABLE"; break;
            case BookTransferError::Timeout: message = "TRANSFER TIMED OUT"; break;
            case BookTransferError::Power: message = "CHARGE BEFORE USING WI-FI"; detail = "Connect USB or charge to 20 percent."; break;
            case BookTransferError::Policy: message = "WI-FI DISABLED BY POLICY"; detail = "Enable Wi-Fi in connectivity policy."; break;
            case BookTransferError::Stop: message = "WIFI STOP FAILED - RETRYING"; detail = "OK retries radio shutdown."; break;
            case BookTransferError::Busy: message = "ANOTHER TRANSFER IS RUNNING"; detail = "Wait for it to finish, then retry."; break;
            case BookTransferError::Credentials: message = "NO SAVED WI-FI NETWORK"; detail = "Choose hotspot mode to send books."; break;
            case BookTransferError::None: message = "STOPPING WI-FI"; detail = "Finishing file cleanup..."; break;
        }
        canvas_.TextCentered(108, message);
        canvas_.TextCentered(158, detail);
    } else {
        DrawFrame(status.state == BookTransferState::Starting ? "STARTING WI-FI" : "SEND BOOKS", "OK Finish  Hold OK Cancel / Back");
        canvas_.Text(16, 56, status.mode == BookTransferMode::Hotspot ? "1. JOIN THIS WI-FI HOTSPOT" : "1. USE THE SAME WI-FI NETWORK");
        DrawUtf8Line(canvas_, 16, 78, status.ssid.data(), 368);
        canvas_.Text(16, 108, status.mode == BookTransferMode::Hotspot ? "WI-FI PASSWORD / WEB ACCESS CODE" : "WEB ACCESS CODE");
        canvas_.Text(16, 130, status.code.data(), 2);
        canvas_.Text(16, 173, "2. OPEN IN YOUR BROWSER");
        char line[64];
        if (status.address[0]) std::snprintf(line, sizeof(line), "http://%s/", status.address.data());
        else std::snprintf(line, sizeof(line), "Waiting for a network address...");
        canvas_.Text(16, 194, line);
        const unsigned percent = status.expected ? static_cast<uint64_t>(status.received) * 100 / status.expected : 0;
        std::snprintf(line, sizeof(line), "%lu BOOKS ADDED   UPLOAD %u%%", static_cast<unsigned long>(status.uploaded), percent);
        canvas_.Text(16, 234, line);
        canvas_.Text(16, 252, "No internet needed. Keep this screen open.");
    }
    return full_refresh ? RefreshFull() : RefreshAuto();
}
