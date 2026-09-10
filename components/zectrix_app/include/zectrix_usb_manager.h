#pragma once

#include "zectrix_host_books.h"
#include "zectrix_navigation.h"
#include "zectrix_sleep_cover.h"

namespace zectrix::storage { class StorageService; }

namespace zectrix::app {

class UsbSettings final : public host::Settings {
public:
    UsbSettings(storage::StorageService& storage, bool& language_saved,
                SleepCoverStyle& cover, bool& cover_saved)
        : storage_(storage), language_saved_(language_saved), cover_(cover), cover_saved_(cover_saved) {}
    host::Status Get(host::Setting key, uint32_t* value) override;
    host::Status Set(host::Setting key, uint32_t value) override;

private:
    storage::StorageService& storage_;
    bool& language_saved_;
    SleepCoverStyle& cover_;
    bool& cover_saved_;
};

enum class UsbDecision : uint8_t { None, RenderFast, RenderQuality, Cancel, Retry, Back, Shutdown };

class UsbManagerController final {
public:
    void Start(bool storage_ready);
    UsbDecision Handle(const sdk::InputEvent& event) const;
    UsbDecision Update(const host::Snapshot& snapshot, int64_t now_us);
    void Presented(bool success) { if (!success) dirty_ = quality_ = true; }
    const host::Snapshot& snapshot() const { return snapshot_; }
    bool storage_ready() const { return storage_ready_; }

private:
    host::Snapshot snapshot_{};
    int64_t last_progress_us_ = 0;
    unsigned drawn_percent_ = 0;
    bool storage_ready_ = false, dirty_ = true, quality_ = true;
};

}  // namespace zectrix::app
