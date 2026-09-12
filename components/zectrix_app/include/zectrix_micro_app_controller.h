#pragma once

#include "zectrix_book_storage.h"
#include "zectrix_runtime.h"
#include "zectrix_scene_manager.h"

namespace zectrix::app {

class MicroAppLibrary {
public:
    virtual ~MicroAppLibrary() = default;
    virtual esp_err_t List(storage::BookEntry* entries, std::size_t capacity, std::size_t* count,
                           bool* more, const char* cursor, bool previous) = 0;
    virtual esp_err_t Open(const char* name, storage::BookFile* file) = 0;
};

enum class MicroAppScene : uint8_t { List, Loading, Running, Error };
enum class MicroAppDecision : uint8_t { None, RenderFast, RenderQuality, Back, Shutdown };

class MicroAppController final {
public:
    void ObserveScenes(SceneSnapshot* snapshot) { scenes_.ObserveScenes(snapshot); }
    static constexpr std::size_t kPageSize = 5;
    explicit MicroAppController(MicroAppLibrary& library) : library_(library) {}
    ~MicroAppController() { Stop(); }
    sdk::Status Start();
    void Stop();
    MicroAppDecision Handle(const sdk::InputEvent& input);
    MicroAppDecision Tick();
    void Presented(bool success);
    MicroAppScene scene() const { return static_cast<MicroAppScene>(scenes_.current()); }
    bool busy() const { return scene() == MicroAppScene::Loading; }
    const storage::BookEntry& entry(std::size_t index) const { return entries_[index]; }
    const package::Metadata* metadata(std::size_t index) const {
        return metadata_[index].icon_side ? &metadata_[index] : nullptr;
    }
    std::size_t count() const { return count_; }
    std::size_t rows() const { return count_ + previous_ + more_; }
    std::size_t selected() const { return selected_; }
    unsigned page() const { return page_; }
    bool previous() const { return previous_; }
    const char* name() const { return name_.data(); }
    const char* title() const { return current_.icon_side ? current_.name.data() : name(); }
    const package::Metadata& current_metadata() const { return current_; }
    esp_err_t storage_result() const { return storage_result_; }
    const runtime::Engine& engine() const { return engine_; }

private:
    static void EnterScene(void* context, SceneId scene);
    static bool OnSceneEvent(void* context, const SceneEvent& event);
    static void ExitScene(void* context, SceneId scene);
    bool OnEvent(const SceneEvent& event);
    void Refresh(const char* cursor = nullptr, bool previous = false);
    void Open();
    void Load();
    void ReleaseSource();
    void GuestResult(bool success, bool render);
    void Invalidate(bool quality = false) { dirty_ = true; quality_ |= quality; }
    MicroAppDecision Decision() const;
    inline static constexpr SceneHandler kHandlers[] = {
        {EnterScene, OnSceneEvent, ExitScene}, {EnterScene, OnSceneEvent, ExitScene},
        {EnterScene, OnSceneEvent, ExitScene}, {EnterScene, OnSceneEvent, ExitScene},
    };
    MicroAppLibrary& library_;
    SceneManager scenes_{kHandlers, std::size(kHandlers), this};
    runtime::Engine engine_;
    storage::BookFile file_;
    std::array<storage::BookEntry, kPageSize> entries_{};
    std::array<package::Metadata, kPageSize> metadata_{};
    package::Metadata current_{};
    package::TextValidator source_text_;
    std::array<char, 64> name_{};
    uint8_t* source_ = nullptr;
    uint32_t loaded_ = 0, source_size_ = 0, source_offset_ = 0;
    std::size_t count_ = 0, selected_ = 0;
    unsigned page_ = 0;
    esp_err_t storage_result_ = ESP_OK;
    bool previous_ = false, more_ = false, dirty_ = false, quality_ = false;
};

}  // namespace zectrix::app
