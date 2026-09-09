#include "zectrix_demo_ui.h"
#include "zectrix_reader_controller.h"
#include "zectrix_book_transfer.h"

#include <algorithm>
#include <cstdio>

namespace {
using namespace zectrix::reader;

void DrawGlyph(ZectrixCanvas& canvas, int x, int y, uint32_t cp, FontSize font, bool inverted = false) {
    const auto* bitmap = GlyphBitmap(cp);
    const int height = FontHeight(font);
    const int width = GlyphWidth(cp, font);
    for (int row = 0; row < height; ++row) {
        const int source_row = row * 16 / height;
        const uint16_t bits = static_cast<uint16_t>(bitmap[1 + source_row * 2]) << 8 |
                              bitmap[2 + source_row * 2];
        for (int col = 0; col < width; ++col)
            if (bits & (0x8000 >> (col * 16 / height))) canvas.Pixel(x + col, y + row, !inverted);
    }
}

uint32_t NextScalar(const char** text) {
    const auto first = static_cast<uint8_t>(*(*text)++);
    if (first < 0x80) return first;
    if (first < 0xc2 || first > 0xf4) return 0xfffd;
    const unsigned count = first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
    uint32_t cp = first & (count == 2 ? 31 : count == 3 ? 15 : 7);
    for (unsigned i = 1; i < count; ++i) {
        const auto byte = static_cast<uint8_t>(**text);
        if (byte < 0x80 || byte > 0xbf) return 0xfffd;
        ++*text;
        cp = (cp << 6) | (byte & 63);
    }
    return cp < (count == 2 ? 0x80U : count == 3 ? 0x800U : 0x10000U) ||
        cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) ? 0xfffd : cp;
}

void DrawName(ZectrixCanvas& canvas, int x, int y, const char* name, int width, bool inverted = false) {
    const int right = x + width;
    while (*name) {
        const auto cp = NextScalar(&name);
        const auto glyph_width = GlyphWidth(cp, FontSize::Small);
        if (x + glyph_width + (*name ? 16 : 0) > right) {
            DrawGlyph(canvas, x, y, 0x2026, FontSize::Small, inverted);
            break;
        }
        DrawGlyph(canvas, x, y, cp, FontSize::Small, inverted);
        x += glyph_width;
    }
}
}

esp_err_t ZectrixDemoUi::ShowReader(const zectrix::app::ReaderController& reader, bool full_refresh) {
    using zectrix::app::ReaderScene;
    const auto& engine = reader.engine();
    if (reader.scene() == ReaderScene::Library) {
        DrawFrame("BOOK LIBRARY", "UP/DOWN Move  OK Open  Hold OK Home");
        const auto& library = reader.library();
        if (!library.count()) {
            canvas_.TextCentered(104, reader.result() == Result::Ok ? "YOUR LIBRARY IS EMPTY" : "BOOK STORAGE UNAVAILABLE");
            canvas_.TextCentered(150, "ADD TXT OR EPUB BOOKS TO START");
            canvas_.TextCentered(196, "OK Retry   Hold OK Home");
        } else {
            constexpr std::size_t rows = 6;
            const auto first = reader.selected() / rows * rows;
            for (std::size_t row = 0; row < rows && first + row < library.count(); ++row) {
                const int y = 52 + row * 32;
                const auto book = library.Get(first + row);
                const bool selected = first + row == reader.selected();
                canvas_.FillRect(8, y, 384, 28, selected);
                if (!selected) canvas_.Rect(8, y, 384, 28);
                DrawName(canvas_, 16, y + 6, book.id.data(), 368, selected);
            }
            char count[64];
            std::snprintf(count, sizeof(count), "%u / %u BOOKS%s",
                static_cast<unsigned>(reader.selected() + 1), static_cast<unsigned>(library.count()),
                library.truncated() ? " (FIRST 32 SHOWN)" : "");
            canvas_.Text(16, 248, count);
        }
    } else if (reader.scene() == ReaderScene::Options) {
        DrawFrame("READING OPTIONS", "UP/DOWN Move  OK Choose  Hold OK Read");
        DrawName(canvas_, 16, 54, reader.book().id.data(), 368);
        const char* options[] = {
            engine.page().font == FontSize::Small ? "FONT: 16 PX -> 24 PX" : "FONT: 24 PX -> 16 PX",
            reader.remote_available() ? "USE PHONE POSITION" : "PHONE POSITION: NONE",
            "READ FROM BEGINNING", "SAVE & RETURN",
        };
        for (std::size_t i = 0; i < std::size(options); ++i) {
            const int y = 84 + i * 40;
            const bool active = reader.option() == i;
            canvas_.FillRect(16, y, 368, 32, active);
            canvas_.Rect(16, y, 368, 32);
            canvas_.Text(28, y + 8, options[i], 1, active);
        }
        canvas_.Text(16, 248, reader.save_result() == Result::Ok ? "PROGRESS SAVED ON EACH PAGE" : "SAVE FAILED - OK: SAVE & RETURN");
    } else {
        const char* footer = "UP Prev  DN Next  OK Options";
        if (reader.busy()) footer = "Loading...  Hold OK Library";
        else if (reader.result() != Result::Ok) footer = "OK Library  Hold OK Back";
        else if (reader.save_result() != Result::Ok) footer = "Not saved. OK: options / retry";
        else if (reader.remote_available()) footer = "Phone progress ready. OK: options";
        else if (engine.has_page() && engine.page().end) footer = "END  UP Prev  OK Options";
        DrawFrame("", footer);
        DrawName(canvas_, 8, 26, reader.book().id.data(), 304, true);
        if (engine.has_page() && (reader.result() == Result::Ok || reader.result() == Result::Pending)) {
            const auto& page = engine.page();
            char progress[16];
            std::snprintf(progress, sizeof(progress), "%u.%u%%", page.progress_per_mille / 10, page.progress_per_mille % 10);
            canvas_.Text(392 - canvas_.TextWidth(progress), 26, progress, 1, true);
            for (std::size_t i = 0; i < page.count; ++i)
                DrawGlyph(canvas_, 8 + page.glyphs[i].x, 48 + page.glyphs[i].y,
                          page.glyphs[i].codepoint, page.font);
            if (!page.count) canvas_.TextCentered(128, "THIS BOOK CONTAINS NO TEXT");
        } else {
            canvas_.TextCentered(120, ResultName(reader.result()));
            canvas_.TextCentered(168, "Hold OK Library");
        }
    }
    return full_refresh ? RefreshFull() : RefreshAuto();
}

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
        DrawFrame("TRANSFER FINISHED", "OK Read books  Hold OK Transfer menu");
        canvas_.TextCentered(86, "WI-FI IS OFF", 2);
        char count[48];
        std::snprintf(count, sizeof(count), "%lu BOOKS ADDED", static_cast<unsigned long>(status.uploaded));
        canvas_.TextCentered(144, count);
        canvas_.TextCentered(194, "Your library is ready to read.");
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
        DrawName(canvas_, 16, 78, status.ssid.data(), 368);
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
