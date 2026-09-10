#include "zectrix_usb_manager.h"

#include "zectrix_first_party_app_controllers.h"
#include "zectrix_language_setting.h"
#include "zectrix_storage_service.h"

namespace zectrix::app {
using host::Status;
using host::Setting;

Status UsbSettings::Get(Setting key, uint32_t* value) {
    if (!value) return Status::Invalid;
    switch (key) {
        case Setting::Language: *value = static_cast<uint32_t>(i18n::CurrentLanguage()); return Status::Ok;
        case Setting::SleepCover: *value = static_cast<uint32_t>(cover_); return Status::Ok;
        case Setting::AutoShowcase: {
            const auto result = storage_.GetUInt32(kAutoShowcaseSettingKey, value);
            if (result == ESP_ERR_NOT_FOUND) { *value = kAutoShowcaseDefault; return Status::Ok; }
            if (result != ESP_OK) return Status::IoError;
            return *value <= 1 ? Status::Ok : Status::Invalid;
        }
    }
    return Status::Invalid;
}

Status UsbSettings::Set(Setting key, uint32_t value) {
    switch (key) {
        case Setting::Language: {
            const auto language = static_cast<i18n::Language>(value);
            if (!i18n::Supports(language)) return Status::Invalid;
            language_saved_ = i18n::SaveLanguage(storage_, language) == ESP_OK;
            return language_saved_ ? Status::Ok : Status::NotSaved;
        }
        case Setting::AutoShowcase:
            if (value > 1) return Status::Invalid;
            return storage_.SetUInt32(kAutoShowcaseSettingKey, value) == ESP_OK ? Status::Ok : Status::IoError;
        case Setting::SleepCover:
            if (value > static_cast<uint32_t>(SleepCoverStyle::Blank)) return Status::Invalid;
            cover_ = static_cast<SleepCoverStyle>(value);
            cover_saved_ = storage_.SetUInt32(kSleepCoverSettingKey, value) == ESP_OK;
            return cover_saved_ ? Status::Ok : Status::NotSaved;
    }
    return Status::Invalid;
}

void UsbManagerController::Start(bool storage_ready) {
    snapshot_ = {};
    storage_ready_ = storage_ready;
    last_progress_us_ = drawn_percent_ = 0;
    dirty_ = quality_ = true;
}

UsbDecision UsbManagerController::Handle(const sdk::InputEvent& event) const {
    switch (MapNavigation(event)) {
        case Navigation::Back: return UsbDecision::Back;
        case Navigation::Shutdown: return UsbDecision::Shutdown;
        case Navigation::Confirm: return storage_ready_ ? UsbDecision::Cancel : UsbDecision::Retry;
        default: return UsbDecision::None;
    }
}

UsbDecision UsbManagerController::Update(const host::Snapshot& snapshot, int64_t now_us) {
    const bool state_changed = snapshot.state != snapshot_.state;
    if ((state_changed && (snapshot.state == host::TransferState::Cancelled || snapshot.state == host::TransferState::Failed)) ||
        snapshot.error != snapshot_.error || snapshot.settings_revision != snapshot_.settings_revision) dirty_ = quality_ = true;
    if (state_changed || snapshot.name != snapshot_.name || snapshot.uploaded != snapshot_.uploaded) dirty_ = true;
    const unsigned percent = snapshot.expected ? static_cast<uint64_t>(snapshot.transferred) * 100 / snapshot.expected : 0;
    if (percent / 10 != drawn_percent_ / 10 && now_us - last_progress_us_ >= 1000000) dirty_ = true;
    snapshot_ = snapshot;
    // Coalesce short files and transfer phases as well as byte progress.
    if (!dirty_ || (!quality_ && now_us - last_progress_us_ < 1000000)) return UsbDecision::None;
    last_progress_us_ = now_us;
    drawn_percent_ = percent;
    const auto result = quality_ ? UsbDecision::RenderQuality : UsbDecision::RenderFast;
    dirty_ = quality_ = false;
    return result;
}

}  // namespace zectrix::app
