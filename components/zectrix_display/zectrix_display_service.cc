#include "zectrix_display_service.h"

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
    *out_service = new DisplayService(static_cast<void*>(handle));
    return ESP_OK;
}

DisplayService::~DisplayService() {
    if (driver_handle_ != nullptr) {
        zectrix_epd_del(static_cast<zectrix_epd_handle_t>(driver_handle_));
    }
}

esp_err_t DisplayService::PowerOn() {
    const esp_err_t err = zectrix_epd_power_on(static_cast<zectrix_epd_handle_t>(driver_handle_));
    if (err != ESP_OK) OnError();
    return err;
}

esp_err_t DisplayService::PowerOff() {
    const esp_err_t err = zectrix_epd_power_off(static_cast<zectrix_epd_handle_t>(driver_handle_));
    if (err != ESP_OK) OnError();
    return err;
}

bool DisplayService::IsPowered() const {
    return zectrix_epd_is_powered(static_cast<zectrix_epd_handle_t>(driver_handle_));
}

esp_err_t DisplayService::RefreshFull1Bpp(const uint8_t* framebuffer, std::size_t size) {
    const esp_err_t err = zectrix_epd_refresh_full_1bpp(
        static_cast<zectrix_epd_handle_t>(driver_handle_), framebuffer, size);
    if (err == ESP_OK) state_model_.OnFull1BppSuccess(); else OnError();
    return err;
}

esp_err_t DisplayService::RefreshPartial1Bpp(const Rect& region, const uint8_t* pixels,
                                             std::size_t size) {
    if (!CanUsePartial() || ShouldRequestFullClean()) return ESP_ERR_INVALID_STATE;
    const zectrix_epd_rect_t raw_region{region.x, region.y, region.width, region.height};
    const esp_err_t err = zectrix_epd_refresh_partial_1bpp(
        static_cast<zectrix_epd_handle_t>(driver_handle_), &raw_region, pixels, size);
    if (err == ESP_OK) state_model_.OnPartial1BppSuccess(region); else OnError();
    return err;
}

esp_err_t DisplayService::RefreshFull4Bpp(const uint8_t* framebuffer, std::size_t size) {
    const esp_err_t err = zectrix_epd_refresh_full_4bpp(
        static_cast<zectrix_epd_handle_t>(driver_handle_), framebuffer, size);
    if (err == ESP_OK) state_model_.OnFull4BppSuccess(); else OnError();
    return err;
}

void DisplayService::OnError() { state_model_.OnRefreshError(); }

}  // namespace zectrix::display
