#include "zectrix_demo_ui.h"
#include "zectrix_reader_controller.h"
#include "zectrix_unicode_text.h"

#include <algorithm>
#include <cstdio>

using namespace zectrix::reader;
using zectrix::ui::DrawGlyph;
using zectrix::ui::DrawUtf8Line;

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
                DrawUtf8Line(canvas_, 16, y + 6, book.id.data(), 368, selected);
            }
            char count[64];
            std::snprintf(count, sizeof(count), "%u / %u BOOKS%s",
                static_cast<unsigned>(reader.selected() + 1), static_cast<unsigned>(library.count()),
                library.truncated() ? " (FIRST 32 SHOWN)" : "");
            if (reader.notice() == zectrix::app::ReaderNotice::None) canvas_.Text(16, 248, count);
        }
        const char* notice = nullptr;
        using Notice = zectrix::app::ReaderNotice;
        switch (reader.notice()) {
            case Notice::None: break;
            case Notice::RecentUnavailable: notice = "RECENT BOOK UNAVAILABLE - CHOOSE A BOOK"; break;
            case Notice::RecentChanged: notice = "RECENT BOOK CHANGED - CHOOSE A BOOK"; break;
            case Notice::HistoryUnavailable: notice = "READING HISTORY UNAVAILABLE"; break;
        }
        if (notice) canvas_.Text(16, 248, notice);
    } else if (reader.scene() == ReaderScene::Options) {
        DrawFrame("READING OPTIONS", "UP/DOWN Move  OK Choose  Hold OK Read");
        DrawUtf8Line(canvas_, 16, 54, reader.book().id.data(), 368);
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
        DrawUtf8Line(canvas_, 8, 26, reader.book().id.data(), 304, true);
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
