#include "zectrix_book_transfer_controller.h"

namespace zectrix::app {
using connectivity::BookTransferState;

sdk::Status BookTransferController::Start() {
    if (scenes_.depth()) return sdk::Status::InvalidState;
    const auto result = scenes_.Start(0);
    dirty_ = quality_ = false;
    return result;
}

void BookTransferController::Stop() {
    scenes_.Stop();
    snapshot_ = {};
    action_ = BookTransferDecision::None;
    last_progress_us_ = drawn_percent_ = drawn_uploaded_ = 0;
    dirty_ = quality_ = false;
}

void BookTransferController::Enter(void* context, SceneId) {
    auto& self = *static_cast<BookTransferController*>(context);
    self.dirty_ = self.quality_ = true;
}

bool BookTransferController::Event(void* context, const SceneEvent& event) {
    auto& self = *static_cast<BookTransferController*>(context);
    if (event.type == SceneEvent::Type::Back) {
        if (self.scene() == BookTransferScene::Session) {
            self.action_ = BookTransferDecision::Stop;
            self.scenes_.Pop();
            return true;
        }
        return false;
    }
    if (event.type != SceneEvent::Type::Input) return false;
    const auto key = MapNavigation(event.input);
    if (self.scene() == BookTransferScene::Mode) {
        if (key == Navigation::Confirm) {
            self.action_ = self.station_selected() ? BookTransferDecision::Station : BookTransferDecision::Hotspot;
            self.scenes_.Push(1);
        } else if (key == Navigation::Previous || key == Navigation::Next) {
            self.scenes_.SetState(0, MoveSelection(self.scenes_.state(0), 2, key));
            self.dirty_ = true;
        } else return false;
    } else if (key == Navigation::Confirm) {
        if (self.snapshot_.state == BookTransferState::Complete) self.action_ = BookTransferDecision::Reader;
        else if (self.snapshot_.state == BookTransferState::Failed || self.snapshot_.state == BookTransferState::Off)
            self.scenes_.Pop();
        else self.action_ = BookTransferDecision::Stop;
    } else return false;
    return true;
}

BookTransferDecision BookTransferController::Handle(const sdk::InputEvent& input) {
    if (!scenes_.depth()) return BookTransferDecision::None;
    const auto key = MapNavigation(input);
    if (key == Navigation::Shutdown) return BookTransferDecision::Shutdown;
    const bool back = key == Navigation::Back;
    if (!scenes_.Dispatch({back ? SceneEvent::Type::Back : SceneEvent::Type::Input, input, 0}) && back)
        return BookTransferDecision::Back;
    return Take();
}

BookTransferDecision BookTransferController::Update(const connectivity::BookTransferSnapshot& snapshot, int64_t now_us) {
    if (!scenes_.depth()) return BookTransferDecision::None;
    const bool changed = snapshot.state != snapshot_.state || snapshot.error != snapshot_.error ||
        snapshot.address != snapshot_.address || snapshot.ssid != snapshot_.ssid || snapshot.code != snapshot_.code;
    const unsigned percent = snapshot.expected ? static_cast<uint64_t>(snapshot.received) * 100 / snapshot.expected : 0;
    if (scene() == BookTransferScene::Session) {
        if (changed) dirty_ = quality_ = true;
        if ((snapshot.uploaded != drawn_uploaded_ || percent / 10 != drawn_percent_ / 10) && now_us - last_progress_us_ >= 1000000) {
            dirty_ = true;
            drawn_percent_ = percent;
            drawn_uploaded_ = snapshot.uploaded;
            last_progress_us_ = now_us;
        }
    }
    snapshot_ = snapshot;
    return Take();
}

BookTransferDecision BookTransferController::Take() {
    const auto result = action_ != BookTransferDecision::None ? action_ : !dirty_ ? BookTransferDecision::None :
        quality_ ? BookTransferDecision::RenderQuality : BookTransferDecision::RenderFast;
    action_ = BookTransferDecision::None;
    dirty_ = quality_ = false;
    return result;
}

}  // namespace zectrix::app
