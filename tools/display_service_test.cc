#include "zectrix_display_service.h"
#include "zectrix_epd.h"

#include <array>
#include <cassert>
#include <cstdlib>
#include <new>
#include <cstring>

struct zectrix_epd_t {
    bool powered = false;
    int power_on_count = 0;
    int power_off_count = 0;
    int full_1bpp_count = 0;
    int partial_count = 0;
    int full_4bpp_count = 0;
    std::array<uint8_t, 64> shadow{};
};

static zectrix_epd_t driver;
static bool fail_service_allocation = false;
static bool fail_next_partial = false;
static int delete_count = 0;
static int64_t now_us = 0;
int64_t esp_timer_get_time() { return now_us += 1000; }

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    if (fail_service_allocation) return nullptr;
    return std::malloc(size);
}

void operator delete(void* pointer, const std::nothrow_t&) noexcept {
    std::free(pointer);
}

void zectrix_epd_get_default_config(zectrix_epd_config_t*) {}
esp_err_t zectrix_epd_new(const zectrix_epd_config_t*,
                          zectrix_epd_handle_t* out_handle) {
    driver = {};
    *out_handle = &driver;
    return ESP_OK;
}
esp_err_t zectrix_epd_del(zectrix_epd_handle_t) {
    ++delete_count;
    return ESP_OK;
}
esp_err_t zectrix_epd_power_on(zectrix_epd_handle_t handle) {
    handle->powered = true;
    ++handle->power_on_count;
    return ESP_OK;
}
esp_err_t zectrix_epd_power_off(zectrix_epd_handle_t handle) {
    handle->powered = false;
    ++handle->power_off_count;
    return ESP_OK;
}
bool zectrix_epd_is_powered(zectrix_epd_handle_t handle) {
    return handle->powered;
}
esp_err_t zectrix_epd_refresh_full_1bpp(zectrix_epd_handle_t handle,
                                        const std::uint8_t* frame, std::size_t) {
    std::memcpy(handle->shadow.data(), frame, handle->shadow.size());
    ++handle->full_1bpp_count;
    return ESP_OK;
}
esp_err_t zectrix_epd_refresh_partial_1bpp(zectrix_epd_handle_t handle,
                                           const zectrix_epd_rect_t*,
                                           const std::uint8_t* pixel, std::size_t) {
    ++handle->partial_count;
    if (fail_next_partial) {
        fail_next_partial = false;
        return ESP_FAIL;
    }
    handle->shadow[0] = pixel[0];
    return ESP_OK;
}

esp_err_t zectrix_epd_copy_shadow(zectrix_epd_handle_t handle, std::size_t offset,
                                  uint8_t* destination, std::size_t size) {
    assert(offset + size <= handle->shadow.size());
    std::memcpy(destination, handle->shadow.data() + offset, size);
    return ESP_OK;
}
esp_err_t zectrix_epd_refresh_full_4bpp(zectrix_epd_handle_t handle,
                                        const std::uint8_t*, std::size_t) {
    ++handle->full_4bpp_count;
    return ESP_OK;
}

int main() {
    using namespace zectrix::display;
    std::array<std::uint8_t, DisplayService::kFrameBytes1Bpp> frame = {};
    std::array<std::uint8_t, DisplayService::kFrameBytes4Bpp> gray_frame = {};
    std::array<std::uint8_t, 1> pixel = {};
    DisplayService* service = nullptr;
    fail_service_allocation = true;
    assert(DisplayService::Create(&service) == ESP_ERR_NO_MEM);
    assert(service == nullptr && delete_count == 1);
    fail_service_allocation = false;
    assert(DisplayService::Create(&service) == ESP_OK);
    DisplayInspection inspection;
    assert(service->ReadInspection(&inspection) == ESP_OK);
    assert(!inspection.framebuffer_valid && inspection.refresh_count == 0);
    assert(service->ReadInspection(nullptr) == ESP_ERR_INVALID_ARG);
    frame[0] = 0x5a;
    assert(service->Present1Bpp(DisplayIntent::Fast,
                                frame.data(), frame.size(),
                                {0, 0, 8, 1}, pixel.data(), 1) == ESP_OK);
    assert(driver.full_1bpp_count == 1 && driver.partial_count == 0);
    assert(driver.power_on_count == 1 && driver.power_off_count == 1);
    assert(service->ReadInspection(&inspection) == ESP_OK);
    assert(inspection.framebuffer_valid && inspection.bits_per_pixel == 1);
    assert(inspection.preview[0] == 0x5a && inspection.last_duration_us == 1000);
    frame[0] = 0x11;
    assert(inspection.preview[0] == 0x5a);
    pixel[0] = 0xa5;
    for (int i = 0; i < 8; ++i) {
        assert(service->Present1Bpp(DisplayIntent::Auto,
                                    frame.data(), frame.size(),
                                    {0, 0, 8, 1}, pixel.data(), 1) == ESP_OK);
    }
    assert(service->state().partial_refresh_count == 8);
    assert(service->ReadInspection(&inspection) == ESP_OK);
    assert(inspection.preview[0] == 0xa5);
    assert(inspection.last_refresh == RefreshKind::kPartial1Bpp);
    assert(service->Present1Bpp(DisplayIntent::Fast,
                                frame.data(), frame.size(),
                                {0, 0, 8, 1}, pixel.data(), 1) == ESP_OK);
    assert(driver.full_1bpp_count == 2);
    assert(service->state().partial_refresh_count == 0);
    assert(service->BeginBatch() == ESP_OK);
    assert(service->Present1Bpp(DisplayIntent::Quality,
                                frame.data(), frame.size()) == ESP_OK);
    assert(service->Present1Bpp(DisplayIntent::Fast,
                                frame.data(), frame.size(),
                                {0, 0, 8, 1}, pixel.data(), 1) == ESP_OK);
    assert(service->EndBatch() == ESP_OK);
    assert(!service->IsPowered());
    assert(driver.power_on_count == driver.power_off_count);
    assert(service->Present4Bpp(DisplayIntent::Fast, frame.data(),
                                frame.size()) == ESP_ERR_NOT_SUPPORTED);
    gray_frame[0] = 0x73;
    assert(service->Present4Bpp(DisplayIntent::Quality, gray_frame.data(),
                                gray_frame.size()) == ESP_OK);
    assert(!service->CanUsePartial());
    assert(service->ReadInspection(&inspection) == ESP_OK);
    assert(inspection.framebuffer_valid && inspection.bits_per_pixel == 4);
    assert(inspection.preview[0] == 0x73 && inspection.framebuffer_bytes == 60000);
    assert(service->Present1Bpp(DisplayIntent::Auto,
                                frame.data(), frame.size(),
                                {0, 0, 8, 1}, pixel.data(), 1) == ESP_OK);
    fail_next_partial = true;
    assert(service->Present1Bpp(DisplayIntent::Auto,
                                frame.data(), frame.size(),
                                {0, 0, 8, 1}, pixel.data(), 1) == ESP_FAIL);
    assert(!service->CanUsePartial());
    assert(service->ReadInspection(&inspection) == ESP_OK);
    assert(!inspection.framebuffer_valid && inspection.last_error == ESP_FAIL);
    assert(inspection.failed_refresh_count == 1);
    const int full_before_recovery = driver.full_1bpp_count;
    assert(service->Present1Bpp(DisplayIntent::Auto,
                                frame.data(), frame.size(),
                                {0, 0, 8, 1}, pixel.data(), 1) == ESP_OK);
    assert(driver.full_1bpp_count == full_before_recovery + 1);
    assert(service->CanUsePartial());
    delete service;
    assert(delete_count == 2);
}
