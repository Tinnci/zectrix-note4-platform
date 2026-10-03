#include "zectrix_locale.h"
#include "ui_engine.h"
#include "zectrix_reader_controller.h"
#include "unicode_text.h"

#include <algorithm>
#include <cstdio>

using zectrix::i18n::Tr;
using zectrix::i18n::Text;

using namespace zectrix::reader;
using zectrix::ui::DrawGlyph;
using zectrix::ui::DrawUtf8Line;

namespace {
const char* ReaderMessage(Result result) {
    switch (result) {
        case Result::Ok: return Tr(Text::ReaderReady);
        case Result::Pending: return Tr(Text::ReaderLoading);
        case Result::End: return Tr(Text::ReaderEnd);
        case Result::Invalid: return Tr(Text::ReaderInvalid);
        case Result::Unsupported: return Tr(Text::ReaderUnsupported);
        case Result::TooLarge: return Tr(Text::ReaderTooLarge);
        case Result::IoError: return Tr(Text::ReaderIoError);
        case Result::NoMemory: return Tr(Text::ReaderNoMemory);
    }
    return Tr(Text::ReaderError);
}
}  // namespace

esp_err_t UiEngine::ShowReader(const zectrix::app::ReaderController& reader, bool full_refresh) {
    using zectrix::app::ReaderScene;
    const auto& engine = reader.engine();
    // Portrait follows the display orientation and uses a taller, narrower page.
    const bool portrait = display_ != nullptr && display_->portrait();
    const int width = portrait ? 300 : 400, height = portrait ? 400 : 300;
    const int dy = (height - 300) / 2;  // centers short messages
    if (reader.scene() == ReaderScene::Library) {
        DrawFrame(Tr(Text::BookLibrary), Tr(Text::NavOpenBack), true);
        const auto& library = reader.library();
        if (!library.count()) {
            WrapText(16, 104 + dy, reader.result() == Result::Ok ? Tr(Text::LibraryEmpty) : Tr(Text::BookStorageUnavailable),
                     width - 32, 18, 2, true);
            WrapText(16, 150 + dy, Tr(Text::AddBooks), width - 32, 18, 2, true);
            WrapText(16, 196 + dy + (portrait ? 18 : 0), Tr(Text::NavRetryLibrary), width - 32, 18, 2, true);
        } else {
            const std::size_t rows = portrait ? 9 : 6;
            const auto first = reader.selected() / rows * rows;
            for (std::size_t row = 0; row < rows && first + row < library.count(); ++row) {
                const int y = 52 + row * 32;
                const auto book = library.Get(first + row);
                const bool selected = first + row == reader.selected();
                canvas_.FillRect(8, y, width - 16, 28, selected);
                if (!selected) canvas_.Rect(8, y, width - 16, 28);
                DrawUtf8Line(canvas_, 16, y + 6, book.id.data(), width - 32, selected);
            }
            char count[64];
            std::snprintf(count, sizeof(count), Tr(Text::BookCount),
                static_cast<unsigned>(reader.selected() + 1), static_cast<unsigned>(library.count()),
                library.truncated() ? Tr(Text::First32Books) : "");
            if (reader.notice() == zectrix::app::ReaderNotice::None) canvas_.TextFitted(16, height - (portrait ? 60 : 52), count, width - 32);
        }
        const char* notice = nullptr;
        using Notice = zectrix::app::ReaderNotice;
        switch (reader.notice()) {
            case Notice::None: break;
            case Notice::RecentUnavailable: notice = Tr(Text::RecentMissing); break;
            case Notice::RecentChanged: notice = Tr(Text::RecentChanged); break;
            case Notice::HistoryUnavailable: notice = Tr(Text::ReadingHistoryUnavailable); break;
        }
        if (notice) WrapText(16, height - (portrait ? 78 : 52), notice, width - 32, 18, portrait ? 2 : 1);
    } else if (reader.scene() == ReaderScene::Options) {
        DrawFrame(Tr(Text::ReadingOptions), Tr(Text::NavReadingOptions), true);
        DrawUtf8Line(canvas_, 16, 54, reader.book().id.data(), width - 32);
        const char* options[] = {
            engine.page().font == FontSize::Small ? Tr(Text::FontSmallToLarge) : Tr(Text::FontLargeToSmall),
            reader.remote_available() ? Tr(Text::UsePhonePosition) : Tr(Text::NoPhonePosition),
            Tr(Text::ReadFromStart), Tr(Text::SaveReturn),
        };
        for (std::size_t i = 0; i < std::size(options); ++i) {
            const int y = 84 + i * (portrait ? 48 : 40);
            const bool active = reader.option() == i;
            canvas_.FillRect(16, y, width - 32, 32, active);
            canvas_.Rect(16, y, width - 32, 32);
            canvas_.TextFitted(28, y + 8, options[i], width - 56, active);
        }
        canvas_.TextFitted(16, height - (portrait ? 60 : 52), reader.save_result() == Result::Ok ? Tr(Text::ProgressAutoSaved) : Tr(Text::ProgressSaveFailed), width - 32);
    } else {
        const char* footer = Tr(Text::NavRead);
        if (reader.busy()) footer = Tr(Text::NavLoading);
        else if (reader.result() != Result::Ok) footer = Tr(Text::NavLibraryBack);
        else if (reader.save_result() != Result::Ok) footer = Tr(Text::NavUnsaved);
        else if (reader.remote_available()) footer = Tr(Text::NavPhonePosition);
        else if (engine.has_page() && engine.page().end) footer = Tr(Text::NavEndOfBook);
        DrawFrame("", footer, true);
        DrawUtf8Line(canvas_, 8, 26, reader.book().id.data(), width - 96, true);
        if (engine.has_page() && (reader.result() == Result::Ok || reader.result() == Result::Pending)) {
            const auto& page = engine.page();
            char progress[16];
            std::snprintf(progress, sizeof(progress), "%u.%u%%", page.progress_per_mille / 10, page.progress_per_mille % 10);
            canvas_.Text(width - 8 - canvas_.TextWidth(progress), 26, progress, 1, true);
            for (std::size_t i = 0; i < page.count; ++i)
                DrawGlyph(canvas_, 8 + page.glyphs[i].x, 48 + page.glyphs[i].y,
                          page.glyphs[i].codepoint, page.font, false, page.glyphs[i].style);
            if (!page.count) canvas_.TextCentered(128 + dy, Tr(Text::BookNoText));
        } else {
            WrapText(16, 120 + dy, ReaderMessage(reader.result()), width - 32, 18, 2, true);
            WrapText(16, 168 + dy, Tr(Text::HoldLibrary), width - 32, 18, 2, true);
        }
    }
    return full_refresh ? RefreshFull() : RefreshAuto();
}
