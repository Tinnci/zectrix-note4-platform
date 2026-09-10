#include "zectrix_locale.h"
#include "zectrix_demo_ui.h"
#include "zectrix_reader_controller.h"
#include "zectrix_unicode_text.h"

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

esp_err_t ZectrixDemoUi::ShowReader(const zectrix::app::ReaderController& reader, bool full_refresh) {
    using zectrix::app::ReaderScene;
    const auto& engine = reader.engine();
    if (reader.scene() == ReaderScene::Library) {
        DrawFrame(Tr(Text::BookLibrary), Tr(Text::NavOpenBack));
        const auto& library = reader.library();
        if (!library.count()) {
            canvas_.TextCentered(104, reader.result() == Result::Ok ? Tr(Text::LibraryEmpty) : Tr(Text::BookStorageUnavailable));
            canvas_.TextCentered(150, Tr(Text::AddBooks));
            canvas_.TextCentered(196, Tr(Text::NavRetryLibrary));
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
            std::snprintf(count, sizeof(count), Tr(Text::BookCount),
                static_cast<unsigned>(reader.selected() + 1), static_cast<unsigned>(library.count()),
                library.truncated() ? Tr(Text::First32Books) : "");
            if (reader.notice() == zectrix::app::ReaderNotice::None) canvas_.Text(16, 248, count);
        }
        const char* notice = nullptr;
        using Notice = zectrix::app::ReaderNotice;
        switch (reader.notice()) {
            case Notice::None: break;
            case Notice::RecentUnavailable: notice = Tr(Text::RecentMissing); break;
            case Notice::RecentChanged: notice = Tr(Text::RecentChanged); break;
            case Notice::HistoryUnavailable: notice = Tr(Text::ReadingHistoryUnavailable); break;
        }
        if (notice) canvas_.Text(16, 248, notice);
    } else if (reader.scene() == ReaderScene::Options) {
        DrawFrame(Tr(Text::ReadingOptions), Tr(Text::NavReadingOptions));
        DrawUtf8Line(canvas_, 16, 54, reader.book().id.data(), 368);
        const char* options[] = {
            engine.page().font == FontSize::Small ? Tr(Text::FontSmallToLarge) : Tr(Text::FontLargeToSmall),
            reader.remote_available() ? Tr(Text::UsePhonePosition) : Tr(Text::NoPhonePosition),
            Tr(Text::ReadFromStart), Tr(Text::SaveReturn),
        };
        for (std::size_t i = 0; i < std::size(options); ++i) {
            const int y = 84 + i * 40;
            const bool active = reader.option() == i;
            canvas_.FillRect(16, y, 368, 32, active);
            canvas_.Rect(16, y, 368, 32);
            canvas_.Text(28, y + 8, options[i], 1, active);
        }
        canvas_.Text(16, 248, reader.save_result() == Result::Ok ? Tr(Text::ProgressAutoSaved) : Tr(Text::ProgressSaveFailed));
    } else {
        const char* footer = Tr(Text::NavRead);
        if (reader.busy()) footer = Tr(Text::NavLoading);
        else if (reader.result() != Result::Ok) footer = Tr(Text::NavLibraryBack);
        else if (reader.save_result() != Result::Ok) footer = Tr(Text::NavUnsaved);
        else if (reader.remote_available()) footer = Tr(Text::NavPhonePosition);
        else if (engine.has_page() && engine.page().end) footer = Tr(Text::NavEndOfBook);
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
            if (!page.count) canvas_.TextCentered(128, Tr(Text::BookNoText));
        } else {
            canvas_.TextCentered(120, ReaderMessage(reader.result()));
            canvas_.TextCentered(168, Tr(Text::HoldLibrary));
        }
    }
    return full_refresh ? RefreshFull() : RefreshAuto();
}
