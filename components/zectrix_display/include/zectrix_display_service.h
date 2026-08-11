#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "zectrix_display_state.h"

namespace zectrix::display {

class DisplayService {
public:
    // Creates a service with the board's default EPD configuration.
    static esp_err_t Create(DisplayService** out_service);
    ~DisplayService();

    DisplayService(const DisplayService&) = delete;
    DisplayService& operator=(const DisplayService&) = delete;

    esp_err_t PowerOn();
    esp_err_t PowerOff();
    bool IsPowered() const;

    esp_err_t RefreshFull1Bpp(const uint8_t* framebuffer, std::size_t size);
    esp_err_t RefreshPartial1Bpp(const Rect& region, const uint8_t* pixels,
                                 std::size_t size);
    esp_err_t RefreshFull4Bpp(const uint8_t* framebuffer, std::size_t size);

    const State& state() const { return state_model_.state(); }
    bool CanUsePartial() const { return state_model_.CanUsePartial(); }
    bool ShouldRequestFullClean() const {
        return state_model_.ShouldRequestFullClean();
    }

private:
    explicit DisplayService(void* driver_handle) : driver_handle_(driver_handle) {}
    void OnError();

    void* driver_handle_;
    StateModel state_model_;
};

}  // namespace zectrix::display
