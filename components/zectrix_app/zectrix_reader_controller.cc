#include "zectrix_reader_controller.h"

namespace zectrix::app {
namespace {
using reader::Result;
constexpr SceneId Id(ReaderScene scene) { return static_cast<SceneId>(scene); }
}

ReaderController::ReaderController(reader::Library& library, reader::Bookmarks& bookmarks,
                                   uint32_t selected)
    : library_(library), bookmarks_(bookmarks) {
    scenes_.SetState(Id(ReaderScene::Library), selected);
}

sdk::Status ReaderController::Start(bool continue_reading) {
    if (started_) return sdk::Status::InvalidState;
    notice_ = ReaderNotice::None;
    save_result_ = bookmarks_.Load();
    if (save_result_ == Result::Ok) bookmarks_.Sync();
    const auto result = scenes_.Start(Id(ReaderScene::Library));
    started_ = sdk::IsOk(result);
    if (started_ && continue_reading) {
        // Resume through an event so the Library parent remains on the stack.
        continue_requested_ = true;
        scenes_.Dispatch({SceneEvent::Type::Tick});
    }
    dirty_ = quality_ = false;
    return result;
}

void ReaderController::SaveDisplayed() {
    if (!needs_save_) return;
    save_result_ = bookmarks_.Save(displayed_);
    if (save_result_ == Result::Ok) needs_save_ = false;
}

void ReaderController::CloseBook() {
    SaveDisplayed();
    bookmarks_.Sync();
    applying_remote_ = false;
    opening_book_ = false;
    resume_only_ = false;
    page_presented_ = false;
    engine_.Close();
    library_.Close();
}

void ReaderController::Stop() {
    if (!started_) return;
    CloseBook();
    scenes_.Stop();
    continue_requested_ = false;
    started_ = false;
}

std::size_t ReaderController::selected() const { return scenes_.state(Id(ReaderScene::Library)); }
std::size_t ReaderController::option() const { return scenes_.state(Id(ReaderScene::Options)); }

bool ReaderController::remote_available() const {
    const auto* remote = bookmarks_.remote();
    return remote && remote->book_id == book_.id && remote->source_bytes == book_.bytes &&
        engine_.ValidPosition(remote->position);
}

void ReaderController::EnterScene(void* context, SceneId scene) {
    auto& self = *static_cast<ReaderController*>(context);
    self.Invalidate(true);
    if (scene == Id(ReaderScene::Library)) {
        self.CloseBook();
        self.result_ = self.library_.Refresh();
        if (self.selected() >= self.library_.count()) self.scenes_.SetState(scene, 0);
    }
}

bool ReaderController::HandleScene(void* context, const SceneEvent& event) {
    return static_cast<ReaderController*>(context)->OnEvent(event);
}

void ReaderController::Operation(Result result) {
    if (result == Result::End) return;
    result_ = result;
    if (result != Result::Pending) {
        if (result != Result::Ok) applying_remote_ = false;
        Invalidate();
    } else {
        Poll();
    }
}

void ReaderController::Poll() {
    if (!engine_.busy()) return;
    result_ = engine_.Poll();
    if (opening_book_ && result_ != Result::Pending) {
        opening_book_ = false;
        if (result_ == Result::Ok) { BeginPage(); return; }
    }
    if (result_ != Result::Pending) {
        if (result_ != Result::Ok) applying_remote_ = false;
        Invalidate(!page_presented_);
    }
}

void ReaderController::ContinueReading() {
    if (save_result_ != Result::Ok) {
        notice_ = ReaderNotice::HistoryUnavailable;
        return;
    }
    const auto* mark = bookmarks_.Latest();
    if (!mark || result_ != Result::Ok) return;
    for (std::size_t i = 0; i < library_.count(); ++i) {
        const auto book = library_.Get(i);
        if (book.id != mark->book_id) continue;
        scenes_.SetState(Id(ReaderScene::Library), i);
        if (book.bytes != mark->source_bytes) notice_ = ReaderNotice::RecentChanged;
        else OpenSelected(true);
        return;
    }
    notice_ = ReaderNotice::RecentUnavailable;
}

void ReaderController::OpenSelected(bool resume_only) {
    notice_ = ReaderNotice::None;
    CloseBook();
    reader::Source* source = nullptr;
    book_ = library_.Get(selected());
    result_ = library_.Open(selected(), &source);
    if (resume_only && (result_ != Result::Ok || !source)) {
        notice_ = ReaderNotice::RecentUnavailable;
        CloseBook();
        result_ = library_.Refresh();
        Invalidate(true);
        return;
    }
    if (result_ == Result::Ok && source) {
        book_.bytes = source->Size();
        // Recheck the opened source: listing metadata can become stale.
        if (resume_only && !bookmarks_.Find(book_.id.data(), book_.bytes)) {
            notice_ = ReaderNotice::RecentChanged;
            CloseBook();
            result_ = library_.Refresh();
            Invalidate(true);
            return;
        }
        resume_only_ = resume_only;
        result_ = engine_.Open(*source, book_.format);
        opening_book_ = result_ == Result::Pending;
        if (result_ == Result::Ok) {
            BeginPage();
            Poll();
        }
    } else if (result_ == Result::Ok) {
        result_ = Result::IoError;
    }
    if (notice_ != ReaderNotice::None) {
        CloseBook();
        result_ = library_.Refresh();
        Invalidate(true);
    } else {
        scenes_.Push(Id(ReaderScene::Reading));
    }
}

void ReaderController::BeginPage() {
    const auto* mark = bookmarks_.Find(book_.id.data(), book_.bytes);
    const bool resume = mark && engine_.ValidPosition(mark->position);
    if (resume_only_ && !resume) {
        notice_ = ReaderNotice::RecentChanged;
        result_ = Result::Invalid;
        return;
    }
    resume_only_ = false;
    result_ = engine_.Seek(resume ? mark->position : reader::Position{},
                           resume ? mark->font : reader::FontSize::Small);
}

bool ReaderController::OnEvent(const SceneEvent& event) {
    if (event.type == SceneEvent::Type::Tick) {
        if (continue_requested_) {
            continue_requested_ = false;
            ContinueReading();
        } else if (scene() == ReaderScene::Reading) {
            Poll();
            if (notice_ != ReaderNotice::None) scenes_.Pop();
        }
        return true;
    }
    if (event.type == SceneEvent::Type::Back) return false;
    if (event.input.action != sdk::InputAction::Click) return false;
    const auto button = event.input.button;
    if (scene() == ReaderScene::Library) {
        if (notice_ != ReaderNotice::None) { notice_ = ReaderNotice::None; Invalidate(); }
        if (button == sdk::Button::Ok) {
            if (library_.count()) OpenSelected();
            else { result_ = library_.Refresh(); Invalidate(); }
        } else if (library_.count()) {
            const auto count = library_.count();
            scenes_.SetState(Id(ReaderScene::Library),
                (selected() + (button == sdk::Button::Up ? count - 1 : 1)) % count);
            Invalidate();
        }
        return true;
    }
    if (scene() == ReaderScene::Reading) {
        if (busy()) return true;
        if (button == sdk::Button::Ok && engine_.has_page() && result_ == Result::Ok) {
            scenes_.Push(Id(ReaderScene::Options));
        } else if (button == sdk::Button::Ok) {
            scenes_.Pop();
        } else if (engine_.has_page()) {
            Operation(button == sdk::Button::Up ? engine_.Previous() : engine_.Next());
        }
        return true;
    }
    if (button == sdk::Button::Up || button == sdk::Button::Down) {
        scenes_.SetState(Id(ReaderScene::Options),
            (option() + (button == sdk::Button::Up ? kOptionCount - 1 : 1)) % kOptionCount);
        Invalidate();
    } else if (button == sdk::Button::Ok) {
        switch (option()) {
            case 0:
                Operation(engine_.SetFont(engine_.page().font == reader::FontSize::Small
                    ? reader::FontSize::Large : reader::FontSize::Small));
                scenes_.Pop();
                break;
            case 1:
                if (remote_available()) {
                    const auto mark = *bookmarks_.remote();
                    applying_remote_ = true;
                    Operation(engine_.Seek(mark.position, mark.font));
                    scenes_.Pop();
                }
                break;
            case 2:
                Operation(engine_.Seek({}, engine_.page().font));
                scenes_.Pop();
                break;
            case 3:
                SaveDisplayed();
                bookmarks_.Sync();
                scenes_.Pop();
                break;
        }
    }
    return true;
}

ReaderDecision ReaderController::Handle(const sdk::InputEvent& input) {
    if (input.button == sdk::Button::Down && input.action == sdk::InputAction::LongPress)
        return ReaderDecision::Shutdown;
    const bool back = input.button == sdk::Button::Ok && input.action == sdk::InputAction::LongPress;
    if (!scenes_.Dispatch({back ? SceneEvent::Type::Back : SceneEvent::Type::Input, input, 0}) && back)
        return ReaderDecision::Home;
    return TakeDecision();
}

ReaderDecision ReaderController::Tick(int64_t now_us) {
    if (now_us >= next_sync_us_) {
        const bool had_remote = remote_available();
        const auto saved = save_result_;
        SaveDisplayed();
        // Preserve the selected incoming revision until the page is presented.
        if (!applying_remote_) bookmarks_.Sync();
        if (saved != save_result_ || had_remote != remote_available()) Invalidate();
        next_sync_us_ = now_us + 5000000;
    }
    scenes_.Dispatch({SceneEvent::Type::Tick, {}, now_us});
    return TakeDecision();
}

void ReaderController::Presented(bool success) {
    if (!success) {
        // Retry through the application so display recovery also saves progress.
        Invalidate(true);
        return;
    }
    if (scene() != ReaderScene::Reading || busy() ||
        !engine_.has_page() || result_ != Result::Ok) return;
    const auto previous_save = save_result_;
    page_presented_ = true;
    if (applying_remote_) {
        save_result_ = bookmarks_.ApplyRemote();
        applying_remote_ = false;
    }
    displayed_ = {};
    displayed_.book_id = book_.id;
    displayed_.source_bytes = book_.bytes;
    displayed_.position = engine_.page().start;
    displayed_.font = engine_.page().font;
    displayed_.progress_per_mille = engine_.page().progress_per_mille;
    needs_save_ = true;
    SaveDisplayed();
    bookmarks_.Sync();
    if (previous_save != save_result_) Invalidate();
}

ReaderDecision ReaderController::TakeDecision() {
    if (!dirty_) return ReaderDecision::None;
    const auto decision = quality_ ? ReaderDecision::RenderQuality : ReaderDecision::RenderFast;
    dirty_ = quality_ = false;
    return decision;
}

}  // namespace zectrix::app
