#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "zectrix_display_state.h"
#include "zectrix_display_inspection.h"

namespace zectrix::display {

enum class DisplayIntent : uint8_t {
    Auto,
    Fast,
    Quality,
    FullClean,
};

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

    // Auto/Fast compare the full frame unless an explicit packed patch is
    // supplied. Unchanged pixels cause no refresh. The full frame remains the
    // fallback when partial use is unavailable or predicted ghosting debt
    // reaches its budget. Large black/white changes also select a full refresh.
    esp_err_t Present1Bpp(DisplayIntent intent,
                          const uint8_t* full_framebuffer,
                          std::size_t full_framebuffer_size,
                          const Rect& partial_region = {},
                          const uint8_t* partial_pixels = nullptr,
                          std::size_t partial_size = 0);
    esp_err_t Present4Bpp(DisplayIntent intent, const uint8_t* framebuffer,
                          std::size_t size);

    const State& state() const { return state_model_.state(); }
    bool CanUsePartial() const { return state_model_.CanUsePartial(); }
    esp_err_t ReadInspection(DisplayInspection* snapshot) const;
    TelemetryBatch ReadTelemetry(uint64_t after = 0) const { return telemetry_.Read(after); }
    // Feed an existing foreground ADC sample; zero millivolts invalidates it.
    void ObserveBattery(uint16_t millivolts, int64_t sampled_us) {
        battery_mv_ = millivolts;
        battery_sampled_us_ = millivolts ? sampled_us : -1;
    }
    bool SetPhysicsParameters(const PhysicsParameters& parameters) { return physics_.SetParameters(parameters); }
    const PhysicsParameters& physics_parameters() const { return physics_.parameters(); }

private:
    explicit DisplayService(void* driver_handle) : driver_handle_(driver_handle) {}
    struct Observation;
    Observation StartObservation() const;
    void StartMetrics(Observation* observation) const;
    esp_err_t BeginRefresh(bool* owns_power);
    esp_err_t EndRefresh(bool owns_power, esp_err_t refresh_result);
    void OnError();
    esp_err_t RecordRefresh(Observation& observation, esp_err_t result,
                            const uint8_t* frame = nullptr);

    void* driver_handle_;
    bool batch_active_ = false;
    StateModel state_model_;
    DisplayInspection inspection_;
    PhysicsModel physics_;
    TelemetryRecorder telemetry_;
    uint16_t battery_mv_ = 0;
    int16_t temperature_centi_c_ = 0;
    int64_t battery_sampled_us_ = -1, temperature_sampled_us_ = -1;
};

}  // namespace zectrix::display
