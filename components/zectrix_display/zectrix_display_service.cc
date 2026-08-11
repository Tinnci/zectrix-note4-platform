#include "zectrix_display_service.h"

#include <new>

#include "zectrix_epd.h"

namespace zectrix::display {

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
            zectrix_epd_power_off(
                static_cast<zectrix_epd_handle_t>(driver_handle_));
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

esp_err_t DisplayService::RefreshFull1Bpp(const uint8_t* framebuffer, std::size_t size) {
    bool owns_power = false;
    esp_err_t err = BeginRefresh(&owns_power);
    if (err != ESP_OK) return err;
    err = zectrix_epd_refresh_full_1bpp(
        static_cast<zectrix_epd_handle_t>(driver_handle_), framebuffer, size);
    if (err == ESP_OK) state_model_.OnFull1BppSuccess(); else OnError();
    return EndRefresh(owns_power, err);
}

esp_err_t DisplayService::RefreshPartial1Bpp(const Rect& region, const uint8_t* pixels,
                                             std::size_t size,
                                             const uint8_t* full_framebuffer,
                                             std::size_t full_framebuffer_size) {
    if (!CanUsePartial()) return ESP_ERR_INVALID_STATE;
    bool owns_power = false;
    esp_err_t err = BeginRefresh(&owns_power);
    if (err != ESP_OK) return err;
    if (state_model_.ShouldRequestFullClean()) {
        if (full_framebuffer == nullptr ||
            full_framebuffer_size != kFrameBytes1Bpp) {
            return EndRefresh(owns_power, ESP_ERR_INVALID_STATE);
        }
        err = zectrix_epd_refresh_full_1bpp(
            static_cast<zectrix_epd_handle_t>(driver_handle_),
            full_framebuffer, full_framebuffer_size);
        if (err == ESP_OK) state_model_.OnFull1BppSuccess(); else OnError();
        return EndRefresh(owns_power, err);
    }
    const zectrix_epd_rect_t raw_region{region.x, region.y, region.width, region.height};
    err = zectrix_epd_refresh_partial_1bpp(
        static_cast<zectrix_epd_handle_t>(driver_handle_), &raw_region, pixels, size);
    if (err == ESP_OK) state_model_.OnPartial1BppSuccess(region); else OnError();
    return EndRefresh(owns_power, err);
}

esp_err_t DisplayService::RefreshFull4Bpp(const uint8_t* framebuffer, std::size_t size) {
    bool owns_power = false;
    esp_err_t err = BeginRefresh(&owns_power);
    if (err != ESP_OK) return err;
    err = zectrix_epd_refresh_full_4bpp(
        static_cast<zectrix_epd_handle_t>(driver_handle_), framebuffer, size);
    if (err == ESP_OK) state_model_.OnFull4BppSuccess(); else OnError();
    return EndRefresh(owns_power, err);
}

esp_err_t DisplayService::BeginRefresh(bool* owns_power) {
    if (owns_power == nullptr) return ESP_ERR_INVALID_ARG;
    *owns_power = false;
    if (batch_active_) return ESP_OK;
    const esp_err_t err = zectrix_epd_power_on(
        static_cast<zectrix_epd_handle_t>(driver_handle_));
    if (err != ESP_OK) {
        OnError();
        return err;
    }
    *owns_power = true;
    return ESP_OK;
}

esp_err_t DisplayService::EndRefresh(bool owns_power,
                                     esp_err_t refresh_result) {
    if (!owns_power) return refresh_result;
    const esp_err_t power_result = zectrix_epd_power_off(
        static_cast<zectrix_epd_handle_t>(driver_handle_));
    if (power_result != ESP_OK) OnError();
    return refresh_result == ESP_OK ? power_result : refresh_result;
}

void DisplayService::OnError() { state_model_.OnRefreshError(); }

}  // namespace zectrix::display
