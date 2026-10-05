#include "note4_boot_guard.h"
#include "note4_connectivity_service.h"
#include "note4_connectivity_settings.h"
#include "note4_edge_page.h"
#include "note4_input_service.h"
#include "note4_log_event.h"
#include "note4_storage_service.h"
#include "terminal_internal.h"
#include <memory>
#if CONFIG_NOTE4_ENABLE_WIFI_HTTP && CONFIG_NOTE4_ENABLE_BOOK_STORAGE
#include "note4_edge_download.h"
#endif

namespace note4::terminal {
namespace {
constexpr char kNextKey[] = "edge_next";
bool WifiPolicy(companion::UserConnectivityPolicy policy) {
    return policy == companion::UserConnectivityPolicy::kAutomatic || policy == companion::UserConnectivityPolicy::kWifiOnly;
}
}
void TerminalApp::LoadEdgeConfiguration() {
    edge_configuration_valid_ = connectivity::StoredEdgeSettings(storage_).Load(&edge_settings_) == ESP_OK && !edge_schedule_failure_;
    uint32_t next = 0;
    const auto read = storage_->GetUInt32(kNextKey, &next);
    if (read != ESP_OK && read != ESP_ERR_NOT_FOUND) edge_configuration_valid_ = false;
    edge_next_sync_at_ = next;
}

uint64_t TerminalApp::NextWakeDelay() {
    const auto power = power_->ReadSnapshot();
    if (power.battery_valid && !power.external_power_present && power.battery_percent <= 5) return 0;
    auto calendar = app::SleepRefreshDelayUs(sleep_cover_style_, time_->Now());
#if CONFIG_NOTE4_ENABLE_BOOK_STORAGE
    if (edge_configuration_valid_ && edge_settings_.show_page && sleep_cover_style_ != app::SleepCoverStyle::Blank) {
        storage::BookFile file;
        uint8_t header[storage::edge::kHeaderSize];
        storage::edge::Page page;
        const auto now = time_->UnixSeconds();
        if (storage_->OpenEdgePage(&file) == ESP_OK && file.Read(0, header, sizeof(header)) &&
            storage::edge::Decode(header, file.Size(), &page) && page.source_generation == edge_settings_.source_generation && page.Fresh(now)) {
            const uint64_t expiry = uint64_t(page.expires_at - now) * 1000000;
            if (!calendar || expiry < calendar) calendar = expiry;
        }
    }
#endif
#if CONFIG_NOTE4_ENABLE_WIFI_HTTP && CONFIG_NOTE4_ENABLE_BOOK_STORAGE
    companion::UserConnectivityPolicy policy;
    const bool policy_valid = connectivity::StoredConnectivitySettings(storage_).LoadPolicy(&policy) == ESP_OK;
    const auto clock = time_->Status();
    const bool allowed = edge_configuration_valid_ && policy_valid && WifiPolicy(policy) && clock.utc_offset_known &&
        connectivity::EdgePowerAllowed(edge_settings_, power.external_power_present,
            power.battery_valid && !power.battery_absent, power.battery_percent);
    return connectivity::PlanWake(edge_settings_, time_->UnixSeconds(), clock.utc_offset_seconds,
        edge_next_sync_at_, calendar, allowed).delay_us;
#else
    return calendar;
#endif
}

bool TerminalApp::RefreshEdgeOnWake() {
#if CONFIG_NOTE4_ENABLE_WIFI_HTTP && CONFIG_NOTE4_ENABLE_BOOK_STORAGE
    const auto health = platform_.Health().Snapshot();
    if (!edge_configuration_valid_ || health.recovery_boot || health.storage_error != ESP_OK ||
        platform_.Boot().ReadBootStatus().confirmation_pending) return false;
    const auto power = power_->ReadSnapshot();
    companion::UserConnectivityPolicy policy;
    const auto clock = time_->Status();
    const auto now = time_->UnixSeconds();
    const bool allowed = clock.utc_offset_known &&
        connectivity::StoredConnectivitySettings(storage_).LoadPolicy(&policy) == ESP_OK && WifiPolicy(policy) &&
        connectivity::EdgePowerAllowed(edge_settings_, power.external_power_present, power.battery_valid && !power.battery_absent, power.battery_percent);
    const auto wake = connectivity::PlanWake(edge_settings_, now, clock.utc_offset_seconds, edge_next_sync_at_, 0, allowed);
    if (!wake.sync_due) return false;
    bool accepted = false, interactive = false;
    uint32_t suggestion = 0;
    {
        connectivity::EdgePageDownload sync(storage_);
        const connectivity::PageTelemetry telemetry{power.battery_valid && !power.battery_absent,
            power.battery_percent, power.battery_mv, power.charging, edge_settings_.interval_seconds};
        if (sync.Begin(edge_settings_, time_->MonotonicMicroseconds() / 1000, telemetry)) {
            connectivity::EdgePageDownload::Result result;
            do {
                input::InputEvent input;
                if (input_->Wait(&input, pdMS_TO_TICKS(10))) {
                    interactive = true;
                    sync.Cancel(time_->MonotonicMicroseconds() / 1000);
                }
                time_->Poll();
                result = sync.Poll(time_->MonotonicMicroseconds() / 1000);
            } while (result == connectivity::EdgePageDownload::Result::Pending);
            if (result == connectivity::EdgePageDownload::Result::StopFailed) {
                interactive = false;
                edge_schedule_failure_ = true; // Do not restart BLE or schedule another burst.
                edge_configuration_valid_ = false;
            }
            auto* data = sync.Data();
            storage::edge::Page page;
            const auto current = time_->UnixSeconds();
            if (result == connectivity::EdgePageDownload::Result::Success && storage::edge::Decode(data, sync.Size(), &page) &&
                page.source_generation == 0 && page.Fresh(current)) {
                storage::BookFile old;
                uint8_t header[storage::edge::kHeaderSize];
                storage::edge::Page previous;
                const bool have_previous = storage_->OpenEdgePage(&old) == ESP_OK && old.Read(0, header, sizeof(header)) &&
                    storage::edge::Decode(header, old.Size(), &previous) && previous.source_generation == edge_settings_.source_generation;
                old.Close();
                if (!have_previous || page.revision > previous.revision) {
                    for (unsigned i = 0; i < 4; ++i) data[24 + i] = edge_settings_.source_generation >> (8 * i);
                    storage::BookStorage* books = nullptr;
                    if (storage_->BeginBookManagement(&books) == ESP_OK) {
                        storage::BookUpload upload;
                        accepted = books->BeginEdgePageUpload(&upload) == storage::BookWriteResult::Ok &&
                            upload.Write(data, sync.Size()) == storage::BookWriteResult::Ok &&
                            upload.Commit() == storage::BookWriteResult::Ok;
                        upload.Abort();
                        if (books->EndManagement() != ESP_OK) accepted = false;
                    }
                } else if (page.revision == previous.revision && page.expires_at == previous.expires_at && page.issued_at == previous.issued_at) {
                    accepted = true;  // Same revision does not wear flash again.
                }
                if (accepted) suggestion = page.next_sync_at;
            }
            NOTE4_LOGI(kTag, "edge_sync", "result=%u cache_saved=%u", static_cast<unsigned>(result),
                       accepted);
        }
    }
    edge_next_sync_at_ = connectivity::NextEdgeSync(edge_settings_, time_->UnixSeconds(), suggestion, !accepted);
    if (storage_->SetUInt32(kNextKey, static_cast<uint32_t>(edge_next_sync_at_)) != ESP_OK) {
        edge_configuration_valid_ = false;  // Never enter a one-second wake loop on persistence failure.
        edge_schedule_failure_ = true;
        NOTE4_LOGW(kTag, "edge_schedule_save_failed", "");
    }
    return interactive;
#else
    return false;
#endif
}

esp_err_t TerminalApp::PresentEdgeCover() {
#if CONFIG_NOTE4_ENABLE_BOOK_STORAGE
    if (!edge_configuration_valid_ || !edge_settings_.show_page || sleep_cover_style_ == app::SleepCoverStyle::Blank) return ESP_ERR_NOT_FOUND;
    storage::BookFile file;
    uint8_t header[storage::edge::kHeaderSize];
    storage::edge::Page page;
    if (storage_->OpenEdgePage(&file) != ESP_OK || !file.Read(0, header, sizeof(header)) ||
        !storage::edge::Decode(header, file.Size(), &page) || page.source_generation != edge_settings_.source_generation ||
        !page.Fresh(time_->UnixSeconds())) return ESP_ERR_NOT_FOUND;
    auto frame = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[storage::edge::kPixelBytes]);
    if (!frame) return ESP_ERR_NO_MEM;
    if (!file.Read(storage::edge::kHeaderSize, frame.get(), storage::edge::kPixelBytes)) return ESP_FAIL;
    return page.portrait ? display_->PresentPortrait1Bpp(frame.get(), storage::edge::kPixelBytes) :
        display_->Present1Bpp(display::DisplayIntent::FullClean, frame.get(), storage::edge::kPixelBytes);
#else
    return ESP_ERR_NOT_FOUND;
#endif
}
}  // namespace note4::terminal
