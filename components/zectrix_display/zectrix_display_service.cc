#include "zectrix_display_service.h"

#include <algorithm>
#include <new>
#include <cstring>

#include "esp_timer.h"
#include "zectrix_epd.h"

namespace zectrix::display {
namespace {

uint32_t Bounded(uint64_t value) { return std::min<uint64_t>(value, UINT32_MAX); }
uint64_t Now() { return std::max<int64_t>(0, esp_timer_get_time()); }
uint32_t Age(int64_t sampled_us, uint64_t now_us) {
    return sampled_us < 0 || static_cast<uint64_t>(sampled_us) > now_us ? UINT32_MAX :
           Bounded((now_us - sampled_us) / 1000);
}

FrameActivity Activity(const zectrix_epd_diff_t& difference, bool valid) {
    FrameActivity activity;
    activity.valid = valid;
    const auto& r = difference.dirty;
    if (r.width) {
        const int left = r.x & ~7, right = (r.x + r.width + 7) & ~7;
        activity.window = {left, r.y, right - left, r.height};
    }
    for (std::size_t tile = 0; tile < kPhysicsTiles; ++tile) {
        activity.tiles[tile] = {difference.tiles[tile].black_to_white, difference.tiles[tile].white_to_black};
    }
    return activity;
}

}  // namespace

struct DisplayService::Observation {
    FrameTelemetry frame;
    zectrix_epd_metrics_t before{};
    bool metrics_known = false;
};

static_assert(DisplayService::kPanelWidth * DisplayService::kPanelHeight == StateModel::kPanelPixels);
static_assert(kPhysicsColumns == ZECTRIX_EPD_TILE_COLUMNS && kPhysicsRows == ZECTRIX_EPD_TILE_ROWS);

esp_err_t DisplayService::Create(DisplayService** out_service) {
    if (out_service == nullptr) return ESP_ERR_INVALID_ARG;
    *out_service = nullptr;
    zectrix_epd_config_t config;
    zectrix_epd_get_default_config(&config);
    zectrix_epd_handle_t handle = nullptr;
    const esp_err_t err = zectrix_epd_new(&config, &handle);
    if (err != ESP_OK) return err;
    *out_service = new (std::nothrow) DisplayService(static_cast<void*>(handle));
    if (*out_service == nullptr) {
        zectrix_epd_del(handle);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

DisplayService::~DisplayService() {
    if (driver_handle_ != nullptr) {
        if (IsPowered()) {
            zectrix_epd_power_off(static_cast<zectrix_epd_handle_t>(driver_handle_));
        }
        zectrix_epd_del(static_cast<zectrix_epd_handle_t>(driver_handle_));
    }
}

esp_err_t DisplayService::BeginBatch() {
    if (batch_active_) return ESP_ERR_INVALID_STATE;
    const esp_err_t err = zectrix_epd_power_on(static_cast<zectrix_epd_handle_t>(driver_handle_));
    if (err != ESP_OK) OnError();
    if (err == ESP_OK) batch_active_ = true;
    return err;
}

esp_err_t DisplayService::EndBatch() {
    if (!batch_active_) return ESP_ERR_INVALID_STATE;
    const esp_err_t err = zectrix_epd_power_off(static_cast<zectrix_epd_handle_t>(driver_handle_));
    batch_active_ = false;
    if (err != ESP_OK) OnError();
    return err;
}

bool DisplayService::IsPowered() const {
    return zectrix_epd_is_powered(static_cast<zectrix_epd_handle_t>(driver_handle_));
}

DisplayService::Observation DisplayService::StartObservation() const {
    Observation observation;
    auto& frame = observation.frame;
    frame.started_us = Now();
    frame.environment = {temperature_centi_c_, battery_mv_, Age(temperature_sampled_us_, frame.started_us),
                         Age(battery_sampled_us_, frame.started_us)};
    frame.model_revision = physics_.parameters().revision;
    frame.gain_q8 = physics_.EnvironmentGain(frame.environment);
    if (batch_active_) frame.flags |= PowerBatch;
    return observation;
}

void DisplayService::StartMetrics(Observation* observation) const {
    // Counters are optional observations: a failed diagnostic copy cannot veto a draw.
    observation->metrics_known = zectrix_epd_read_metrics(
        static_cast<zectrix_epd_handle_t>(driver_handle_), &observation->before) == ESP_OK;
}

esp_err_t DisplayService::Present1Bpp(
    DisplayIntent intent, const uint8_t* full_framebuffer,
    std::size_t full_framebuffer_size, const Rect& partial_region,
    const uint8_t* partial_pixels, std::size_t partial_size) {
    if (full_framebuffer == nullptr || full_framebuffer_size != kFrameBytes1Bpp)
        return ESP_ERR_INVALID_ARG;
    if (intent != DisplayIntent::Auto && intent != DisplayIntent::Fast &&
        intent != DisplayIntent::Quality && intent != DisplayIntent::FullClean) return ESP_ERR_INVALID_ARG;
    const bool automatic = intent == DisplayIntent::Auto || intent == DisplayIntent::Fast;
    const bool has_patch = automatic && (partial_pixels != nullptr || partial_size != 0 ||
        partial_region.x != 0 || partial_region.y != 0 || partial_region.width != 0 || partial_region.height != 0);
    const Rect source = has_patch ? partial_region : Rect{0, 0, kPanelWidth, kPanelHeight};
    const auto* pixels = has_patch ? partial_pixels : full_framebuffer;
    const auto size = has_patch ? partial_size : full_framebuffer_size;
    const zectrix_epd_rect_t raw_source{source.x, source.y, source.width, source.height};
    zectrix_epd_diff_t difference{};
    auto observation = StartObservation();
    auto& frame = observation.frame;
    auto handle = static_cast<zectrix_epd_handle_t>(driver_handle_);
    esp_err_t err = zectrix_epd_analyze_1bpp(handle, &raw_source, pixels, size, &difference);
    if (err == ESP_ERR_INVALID_ARG || err == ESP_ERR_INVALID_SIZE) return err;
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        OnError();
        frame.kind = automatic ? RefreshKind::kPartial1Bpp : RefreshKind::kFull1Bpp;
        frame.reason = RefreshReason::DriverError;
        return RecordRefresh(observation, err);
    }
    auto activity = Activity(difference, err == ESP_OK && CanUsePartial());
    // Equal submissions never power the panel, record a frame or relax its debt.
    if (automatic && activity.valid && difference.changed_pixels == 0) return ESP_OK;
    const auto prediction = physics_.Predict(activity, frame.environment, frame.started_us);
    frame.reason = automatic ? prediction.reason :
        intent == DisplayIntent::Quality ? RefreshReason::Quality : RefreshReason::FullClean;
    frame.projected_mean_q16 = prediction.next.Mean();
    frame.projected_peak_q16 = prediction.next.Peak();
    const bool full = frame.reason != RefreshReason::Partial;
    frame.kind = full ? RefreshKind::kFull1Bpp : RefreshKind::kPartial1Bpp;
    if (full && has_patch && activity.valid) {
        // Cleanup presents the supplied fallback, which may differ from the patch.
        // Keep the patch-based prediction but observe the pixels actually sent.
        const zectrix_epd_rect_t screen{0, 0, kPanelWidth, kPanelHeight};
        const auto analyzed = zectrix_epd_analyze_1bpp(handle, &screen, full_framebuffer,
                                                     full_framebuffer_size, &difference);
        activity = Activity(difference, analyzed == ESP_OK);
    }
    frame.window = full ? Rect{0, 0, kPanelWidth, kPanelHeight} : activity.window;
    if (activity.valid) {
        frame.flags |= TransitionsKnown;
        for (const auto& tile : activity.tiles) {
            frame.black_to_white += tile.black_to_white;
            frame.white_to_black += tile.white_to_black;
        }
    }
    StartMetrics(&observation);
    bool owns_power = false;
    err = BeginRefresh(&owns_power);
    if (err == ESP_OK) {
        err = full ? zectrix_epd_refresh_full_1bpp(handle, full_framebuffer, full_framebuffer_size) :
                     zectrix_epd_refresh_partial_1bpp(handle, &raw_source, pixels, size);
        err = EndRefresh(owns_power, err);
    }
    if (err == ESP_OK) {
        if (full) {
            state_model_.OnFull1BppSuccess();
            physics_.Clean(Now());
        } else {
            const auto& dirty = difference.dirty;
            state_model_.OnPartial1BppSuccess({dirty.x, dirty.y, dirty.width, dirty.height}, difference.changed_pixels);
            physics_.Commit(prediction, Now());
        }
    } else OnError();
    return RecordRefresh(observation, err, full ? full_framebuffer : nullptr);
}

esp_err_t DisplayService::Present4Bpp(DisplayIntent intent, const uint8_t* framebuffer, std::size_t size) {
    if (intent != DisplayIntent::Quality) return ESP_ERR_NOT_SUPPORTED;
    if (framebuffer == nullptr || size != kFrameBytes4Bpp) return ESP_ERR_INVALID_ARG;
    auto observation = StartObservation();
    observation.frame.kind = RefreshKind::kFull4Bpp;
    observation.frame.reason = RefreshReason::Gray;
    observation.frame.window = {0, 0, kPanelWidth, kPanelHeight};
    StartMetrics(&observation);
    bool owns_power = false;
    esp_err_t err = BeginRefresh(&owns_power);
    if (err == ESP_OK) {
        err = zectrix_epd_refresh_full_4bpp(static_cast<zectrix_epd_handle_t>(driver_handle_), framebuffer, size);
        err = EndRefresh(owns_power, err);
    }
    if (err == ESP_OK) {
        state_model_.OnFull4BppSuccess();
        physics_.Clean(Now());
    } else OnError();
    return RecordRefresh(observation, err, framebuffer);
}

esp_err_t DisplayService::BeginRefresh(bool* owns_power) {
    if (owns_power == nullptr) return ESP_ERR_INVALID_ARG;
    *owns_power = false;
    if (batch_active_) return ESP_OK;
    const esp_err_t err = zectrix_epd_power_on(static_cast<zectrix_epd_handle_t>(driver_handle_));
    if (err == ESP_OK) *owns_power = true;
    return err;
}

esp_err_t DisplayService::EndRefresh(bool owns_power, esp_err_t refresh_result) {
    if (!owns_power) return refresh_result;
    const esp_err_t power_result = zectrix_epd_power_off(static_cast<zectrix_epd_handle_t>(driver_handle_));
    return refresh_result == ESP_OK ? power_result : refresh_result;
}

void DisplayService::OnError() {
    state_model_.OnRefreshError();
    inspection_.framebuffer_valid = false;
}

esp_err_t DisplayService::RecordRefresh(Observation& observation, esp_err_t result, const uint8_t* pixels) {
    auto& frame = observation.frame;
    const auto now = Now();
    frame.duration_us = Bounded(now >= frame.started_us ? now - frame.started_us : 0);
    frame.error = result;
    frame.committed_mean_q16 = physics_.state().Mean();
    frame.committed_peak_q16 = physics_.state().Peak();
    zectrix_epd_metrics_t after{};
    if (zectrix_epd_read_metrics(static_cast<zectrix_epd_handle_t>(driver_handle_), &after) == ESP_OK) {
        temperature_centi_c_ = after.temperature_centi_c;
        temperature_sampled_us_ = after.temperature_sampled_us;
        if (observation.metrics_known) {
            const auto& before = observation.before;
            frame.flags |= DriverMetricsKnown;
            frame.spi_bytes = Bounded(after.spi_bytes - before.spi_bytes);
            frame.ram_bytes = Bounded(after.ram_bytes - before.ram_bytes);
            frame.busy_us = Bounded(after.busy_us - before.busy_us);
            frame.refresh_busy_us = Bounded(after.refresh_busy_us - before.refresh_busy_us);
            frame.waveform_triggers = std::min<uint32_t>(after.refresh_triggers - before.refresh_triggers, UINT8_MAX);
            if (after.temperature_sampled_us >= 0 && after.temperature_sampled_us != before.temperature_sampled_us) {
                frame.panel_temperature_centi_c = after.temperature_centi_c;
                frame.flags |= PanelTemperatureRead;
            }
            if (result == ESP_OK && physics_.EstimateEnergy(frame.kind, frame.busy_us, frame.spi_bytes,
                    frame.flags & TransitionsKnown, frame.black_to_white, frame.white_to_black, &frame.energy_uj))
                frame.flags |= EnergyEstimated;
        }
    }
    telemetry_.Record(frame);
    inspection_.last_refresh = frame.kind;
    inspection_.last_reason = frame.reason;
    inspection_.last_error = result;
    inspection_.last_duration_us = frame.duration_us;
    ++inspection_.refresh_count;
    if (result != ESP_OK) {
        ++inspection_.failed_refresh_count;
        inspection_.framebuffer_valid = false;
        return result;
    }
    inspection_.bits_per_pixel = frame.kind == RefreshKind::kFull4Bpp ? 4 : 1;
    inspection_.framebuffer_bytes = inspection_.bits_per_pixel == 4 ? kFrameBytes4Bpp : kFrameBytes1Bpp;
    if (pixels != nullptr) {
        std::memcpy(inspection_.preview.data(), pixels, inspection_.preview.size());
        inspection_.framebuffer_valid = true;
    } else {
        inspection_.framebuffer_valid = zectrix_epd_copy_shadow(
            static_cast<zectrix_epd_handle_t>(driver_handle_), 0,
            inspection_.preview.data(), inspection_.preview.size()) == ESP_OK;
    }
    return result;
}

esp_err_t DisplayService::ReadInspection(DisplayInspection* snapshot) const {
    if (snapshot == nullptr) return ESP_ERR_INVALID_ARG;
    *snapshot = inspection_;
    snapshot->state = state_model_.state();
    snapshot->powered = IsPowered();
    snapshot->batch_active = batch_active_;
    snapshot->debt_mean_q16 = physics_.state().Mean();
    snapshot->debt_peak_q16 = physics_.state().Peak();
    snapshot->global_limit_q16 = physics_.parameters().global_limit_q16;
    snapshot->local_limit_q16 = physics_.parameters().local_limit_q16;
    snapshot->model_revision = physics_.parameters().revision;
    return ESP_OK;
}

}  // namespace zectrix::display
