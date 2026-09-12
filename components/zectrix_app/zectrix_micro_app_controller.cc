#include "zectrix_micro_app_controller.h"
#include "zectrix_app_storage.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

namespace zectrix::app {
namespace {
constexpr SceneId Id(MicroAppScene scene) { return static_cast<SceneId>(scene); }
static_assert(package::kSourceLimit == runtime::kSourceLimit);
static_assert(package::kInstructionLimit == runtime::kInstructionLimit);

bool ReadMetadata(storage::BookFile& file, package::Metadata& metadata) {
    std::array<uint8_t, package::kHeaderSize> header{};
    package::Metadata candidate;
    if (!file.Read(0, header.data(), header.size()) ||
        !package::ReadHeader(header.data(), header.size(), file.Size(), candidate) ||
        !file.Read(header.size(), candidate.icon.data(), candidate.IconBytes())) return false;
    metadata = candidate;
    return true;
}
}

sdk::Status MicroAppController::Start() {
    Stop();
    selected_ = page_ = 0;
    previous_ = more_ = false;
    Refresh();
    return scenes_.Start(Id(MicroAppScene::List));
}

void MicroAppController::Stop() {
    scenes_.Stop();
    ReleaseSource();
    engine_.Stop();
    dirty_ = quality_ = false;
}

void MicroAppController::Refresh(const char* cursor, bool previous) {
    // Copy the cursor before overwriting the page it may point into.
    std::array<char, 64> boundary{};
    if (cursor) std::strcpy(boundary.data(), cursor);
    bool more = false;
    storage_result_ = library_.List(entries_.data(), entries_.size(), &count_, &more,
                                    cursor ? boundary.data() : nullptr, previous);
    metadata_ = {};
    if (storage_result_ == ESP_OK) {
        for (std::size_t i = 0; i < count_; ++i) {
            if (!storage::AppStorage::Packaged(entries_[i].name.data())) continue;
            // Discovery reads at most 288 bytes per entry and never loads Lua.
            storage::BookFile file;
            if (library_.Open(entries_[i].name.data(), &file) == ESP_OK) ReadMetadata(file, metadata_[i]);
        }
    }
    if (storage_result_ == ESP_OK && !count_ && cursor) { Refresh(); return; }
    selected_ = 0;
    if (storage_result_ != ESP_OK) {
        count_ = page_ = 0;
        previous_ = more_ = false;
    } else if (!cursor) {
        page_ = 0;
        previous_ = false;
        more_ = more;
    } else if (previous) {
        if (page_) --page_;
        previous_ = more;
        more_ = true;
    } else {
        ++page_;
        previous_ = true;
        more_ = more;
    }
    Invalidate(true);
}

void MicroAppController::EnterScene(void* context, SceneId scene) {
    auto& self = *static_cast<MicroAppController*>(context);
    if (scene == Id(MicroAppScene::Loading)) self.Open();
    self.Invalidate(true);
}

void MicroAppController::ExitScene(void* context, SceneId scene) {
    auto& self = *static_cast<MicroAppController*>(context);
    if (scene == Id(MicroAppScene::Loading)) self.ReleaseSource();
    if (scene == Id(MicroAppScene::Running)) self.engine_.Stop();
}

bool MicroAppController::OnSceneEvent(void* context, const SceneEvent& event) {
    return static_cast<MicroAppController*>(context)->OnEvent(event);
}

void MicroAppController::Open() {
    engine_.Stop();
    ReleaseSource();
    current_ = {};
    source_text_ = {};
    storage_result_ = library_.Open(name_.data(), &file_);
    if (storage_result_ != ESP_OK) return;
    if (storage::AppStorage::Packaged(name_.data())) {
        if (!ReadMetadata(file_, current_)) {
            storage_result_ = ESP_ERR_INVALID_ARG;
            return;
        }
        source_size_ = current_.source_size;
        source_offset_ = current_.SourceOffset();
    } else {
        source_size_ = file_.Size();
    }
    if (!source_size_ || source_size_ > runtime::kSourceLimit) {
        storage_result_ = ESP_ERR_INVALID_SIZE;
        return;
    }
#ifdef ESP_PLATFORM
    source_ = static_cast<uint8_t*>(heap_caps_malloc(source_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
    source_ = static_cast<uint8_t*>(std::malloc(source_size_));
#endif
    if (!source_) storage_result_ = ESP_ERR_NO_MEM;
}

void MicroAppController::ReleaseSource() {
    file_.Close();
#ifdef ESP_PLATFORM
    heap_caps_free(source_);
#else
    std::free(source_);
#endif
    source_ = nullptr;
    loaded_ = source_size_ = source_offset_ = 0;
}

void MicroAppController::Load() {
    if (storage_result_ != ESP_OK) {
        scenes_.Replace(Id(MicroAppScene::Error));
        return;
    }
    const auto size = std::min<uint32_t>(1024, source_size_ - loaded_);
    if (!file_.Read(source_offset_ + loaded_, source_ + loaded_, size)) {
        storage_result_ = ESP_FAIL;
        scenes_.Replace(Id(MicroAppScene::Error));
        return;
    }
    if (source_offset_ && !source_text_.Feed(source_ + loaded_, size)) {
        storage_result_ = ESP_ERR_INVALID_ARG;
        scenes_.Replace(Id(MicroAppScene::Error));
        return;
    }
    loaded_ += size;
    if (loaded_ != source_size_) return;
    if (source_offset_ && !source_text_.Complete()) {
        storage_result_ = ESP_ERR_INVALID_ARG;
        scenes_.Replace(Id(MicroAppScene::Error));
        return;
    }
    runtime::Options options;
    if (source_offset_) {
        options = {static_cast<int>(current_.instructions),
            bool(current_.permissions & package::kDisplay), bool(current_.permissions & package::kInput)};
    }
    const bool started = engine_.Start(source_, loaded_, options);
    ReleaseSource();
    if (started && engine_.exit_requested()) {
        engine_.Stop();
        scenes_.Pop();
    } else if (started && engine_.Draw()) {
        scenes_.Replace(Id(MicroAppScene::Running));
    } else {
        scenes_.Replace(Id(MicroAppScene::Error));
    }
}

void MicroAppController::GuestResult(bool success, bool render) {
    if (success && engine_.exit_requested()) {
        scenes_.Pop();
    } else if (!success || (render && !engine_.Draw())) {
        scenes_.Replace(Id(MicroAppScene::Error));
    } else if (render) {
        Invalidate();
    }
}

bool MicroAppController::OnEvent(const SceneEvent& event) {
    if (event.type == SceneEvent::Type::Back) return false;
    if (event.type == SceneEvent::Type::Tick) {
        if (busy()) Load();
        return true;
    }
    const auto key = MapNavigation(event.input);
    if (scene() == MicroAppScene::List) {
        if (key == Navigation::Confirm) {
            if (!rows()) Refresh();
            else if (selected_ < count_) {
                std::strcpy(name_.data(), entries_[selected_].name.data());
                scenes_.Push(Id(MicroAppScene::Loading));
            } else if (previous_ && selected_ == count_) {
                Refresh(entries_[0].name.data(), true);
            } else if (count_) {
                Refresh(entries_[count_ - 1].name.data());
            }
        } else if (rows() && (key == Navigation::Previous || key == Navigation::Next)) {
            selected_ = key == Navigation::Previous ? (selected_ + rows() - 1) % rows() : (selected_ + 1) % rows();
            Invalidate();
        } else return false;
    } else if (scene() == MicroAppScene::Error && key == Navigation::Confirm) {
        scenes_.Pop();
    } else if (scene() == MicroAppScene::Running) {
        runtime::Key guest_key;
        if (key == Navigation::Previous) guest_key = runtime::Key::Up;
        else if (key == Navigation::Next) guest_key = runtime::Key::Down;
        else if (key == Navigation::Confirm) guest_key = runtime::Key::Ok;
        else return false;
        const bool result = engine_.Handle(guest_key);
        GuestResult(result, engine_.redraw_requested());
    } else return false;
    return true;
}

MicroAppDecision MicroAppController::Handle(const sdk::InputEvent& input) {
    const auto key = MapNavigation(input);
    if (key == Navigation::Shutdown) return MicroAppDecision::Shutdown;
    const auto type = key == Navigation::Back ? SceneEvent::Type::Back : SceneEvent::Type::Input;
    if (!scenes_.Dispatch({type, input}) && type == SceneEvent::Type::Back) return MicroAppDecision::Back;
    return Decision();
}

MicroAppDecision MicroAppController::Tick() {
    scenes_.Dispatch({SceneEvent::Type::Tick});
    return Decision();
}

MicroAppDecision MicroAppController::Decision() const {
    return !dirty_ ? MicroAppDecision::None : quality_ ? MicroAppDecision::RenderQuality : MicroAppDecision::RenderFast;
}

void MicroAppController::Presented(bool success) {
    dirty_ = quality_ = !success;
}

}  // namespace zectrix::app
