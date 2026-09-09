#pragma once

#include "zectrix_scene_manager.h"
#include "zectrix_book_transfer.h"

namespace zectrix::app {

enum class BookTransferScene : uint8_t { Mode, Session };
enum class BookTransferDecision : uint8_t { None, RenderFast, RenderQuality, Hotspot, Station, Stop, Reader, Home, Shutdown };

class BookTransferController {
public:
    sdk::Status Start();
    BookTransferDecision Handle(const sdk::InputEvent& input);
    BookTransferDecision Update(const connectivity::BookTransferSnapshot& snapshot, int64_t now_us);
    void Presented(bool success) { if (!success) dirty_ = quality_ = true; }
    BookTransferScene scene() const { return static_cast<BookTransferScene>(scenes_.current()); }
    bool station_selected() const { return scenes_.state(0) == 1; }
    const connectivity::BookTransferSnapshot& snapshot() const { return snapshot_; }

private:
    static void Enter(void* context, SceneId);
    static bool Event(void* context, const SceneEvent& event);
    BookTransferDecision Take();
    inline static constexpr SceneHandler kHandlers[] = {{Enter, Event, nullptr}, {Enter, Event, nullptr}};
    SceneManager scenes_{kHandlers, std::size(kHandlers), this};
    connectivity::BookTransferSnapshot snapshot_{};
    BookTransferDecision action_ = BookTransferDecision::None;
    int64_t last_progress_us_ = 0;
    unsigned drawn_percent_ = 0;
    uint32_t drawn_uploaded_ = 0;
    bool dirty_ = false, quality_ = false;
};

}  // namespace zectrix::app
