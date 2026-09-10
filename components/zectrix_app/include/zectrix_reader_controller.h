#pragma once

#include "zectrix_reader_bookmarks.h"
#include "zectrix_reader_library.h"
#include "zectrix_scene_manager.h"

namespace zectrix::app {

enum class ReaderScene : uint8_t { Library, Reading, Options };
enum class ReaderDecision : uint8_t { None, RenderFast, RenderQuality, Home, Shutdown };
enum class ReaderNotice : uint8_t { None, RecentUnavailable, RecentChanged, HistoryUnavailable };

class ReaderController {
public:
    static constexpr std::size_t kOptionCount = 4;
    ReaderController(reader::Library& library, reader::Bookmarks& bookmarks, uint32_t selected = 0);
    ~ReaderController() { Stop(); }
    sdk::Status Start(bool continue_reading = false);
    void Stop();
    ReaderDecision Handle(const sdk::InputEvent& input);
    ReaderDecision Tick(int64_t now_us);
    // Only a successful display commit advances the local reading bookmark.
    void Presented(bool success);
    ReaderScene scene() const { return static_cast<ReaderScene>(scenes_.current()); }
    std::size_t selected() const;
    std::size_t option() const;
    const reader::Library& library() const { return library_; }
    const reader::BookInfo& book() const { return book_; }
    const reader::Engine& engine() const { return engine_; }
    reader::Result result() const { return result_; }
    reader::Result save_result() const { return save_result_; }
    ReaderNotice notice() const { return notice_; }
    bool remote_available() const;
    bool busy() const { return engine_.busy(); }

private:
    static void EnterScene(void* context, SceneId scene);
    static bool HandleScene(void* context, const SceneEvent& event);
    bool OnEvent(const SceneEvent& event);
    void ContinueReading();
    void OpenSelected(bool resume_only = false);
    void BeginPage();
    void Poll();
    void Operation(reader::Result result);
    void SaveDisplayed();
    void CloseBook();
    ReaderDecision TakeDecision();
    void Invalidate(bool quality = false) { dirty_ = true; quality_ |= quality; }
    inline static constexpr SceneHandler kHandlers[] = {
        {EnterScene, HandleScene, nullptr}, {EnterScene, HandleScene, nullptr},
        {EnterScene, HandleScene, nullptr},
    };
    reader::Library& library_;
    reader::Bookmarks& bookmarks_;
    reader::Engine engine_;
    SceneManager scenes_{kHandlers, std::size(kHandlers), this};
    reader::BookInfo book_{};
    reader::Bookmark displayed_{};
    reader::Result result_ = reader::Result::Ok;
    reader::Result save_result_ = reader::Result::Ok;
    ReaderNotice notice_ = ReaderNotice::None;
    int64_t next_sync_us_ = 0;
    bool needs_save_ = false;
    bool applying_remote_ = false;
    bool opening_book_ = false;
    bool continue_requested_ = false;
    bool resume_only_ = false;
    bool page_presented_ = false;
    bool dirty_ = false;
    bool quality_ = false;
    bool started_ = false;
};

}  // namespace zectrix::app
