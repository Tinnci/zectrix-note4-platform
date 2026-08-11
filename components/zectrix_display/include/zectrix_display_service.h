#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "zectrix_display_state.h"

namespace zectrix::display {

class DisplayService {
public:
    static constexpr int kPanelWidth = 400;
    static constexpr int kPanelHeight = 300;
    static constexpr std::size_t kFrameBytes1Bpp = 15000;
    static constexpr std::size_t kFrameBytes4Bpp = 60000;

    // Creates a service with the board's default EPD configuration.
    static esp_err_t Create(DisplayService** out_service);
    ~DisplayService();

    DisplayService(const DisplayService&) = delete;
    DisplayService& operator=(const DisplayService&) = delete;

    // Call all methods from one application task. A batch has task-local
    // sequencing semantics and the service does not support concurrent calls.
    // Keep the panel powered across a related refresh sequence. The service
    // remains the owner of the panel power operation.
    esp_err_t BeginBatch();
    esp_err_t EndBatch();
    bool IsPowered() const;

    esp_err_t RefreshFull1Bpp(const uint8_t* framebuffer, std::size_t size);
    esp_err_t RefreshPartial1Bpp(const Rect& region, const uint8_t* pixels,
                                 std::size_t size,
                                 const uint8_t* full_framebuffer = nullptr,
                                 std::size_t full_framebuffer_size = 0);
    esp_err_t RefreshFull4Bpp(const uint8_t* framebuffer, std::size_t size);

    const State& state() const { return state_model_.state(); }
    bool CanUsePartial() const { return state_model_.CanUsePartial(); }

private:
    explicit DisplayService(void* driver_handle) : driver_handle_(driver_handle) {}
    esp_err_t BeginRefresh(bool* owns_power);
    esp_err_t EndRefresh(bool owns_power, esp_err_t refresh_result);
    void OnError();

    void* driver_handle_;
    bool batch_active_ = false;
    StateModel state_model_;
};

}  // namespace zectrix::display
