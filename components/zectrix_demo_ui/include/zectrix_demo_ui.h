#ifndef ZECTRIX_DEMO_UI_H_
#define ZECTRIX_DEMO_UI_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "esp_err.h"
#include "zectrix_canvas.h"
#include "zectrix_display_service.h"
#include "zectrix_power_service.h"
#include "zectrix_self_test.h"
#include "zectrix_system_service.h"
#include "zectrix_time_service.h"
#include "zectrix_status_bar.h"
#include "zectrix_view_port.h"

namespace zectrix::app { class ReaderController; }
namespace zectrix::app { class LauncherController; struct ReadingOverview; }
namespace zectrix::app { struct SleepCoverSnapshot; enum class SleepCoverStyle : uint8_t; }
namespace zectrix::connectivity { struct BookTransferSnapshot; }

class ZectrixDemoUi {
public:
    explicit ZectrixDemoUi(zectrix::display::DisplayService* display);
    ZectrixDemoUi(const ZectrixDemoUi&) = delete;
    ZectrixDemoUi& operator=(const ZectrixDemoUi&) = delete;
    void SetDisplay(zectrix::display::DisplayService* display) {
        display_ = display;
    }
    void SetTime(zectrix::time::TimeService* time) { time_ = time; }
    void UpdateStatus(const zectrix::ui::StatusBarState& state);
    esp_err_t RefreshPending();
    esp_err_t ShowImage1Bpp(const uint8_t* pixels, size_t size);
    esp_err_t ShowImage4Bpp(const uint8_t* pixels, size_t size);
    esp_err_t ShowImagePatch(zectrix::display::Rect region,
                             const uint8_t* pixels, size_t size);

    esp_err_t ShowSplash();
    esp_err_t ShowLauncher(const zectrix::app::LauncherController& launcher,
                           const zectrix::time::ClockSnapshot& clock,
                           const zectrix::app::ReadingOverview& reading, bool full_refresh);
    esp_err_t ShowMenu(const char* title, const char* const* items,
                       size_t count, size_t selected, const char* footer,
                       bool full_refresh);
    esp_err_t ShowSceneInfo(const char* title, const char* mode,
                            const char* format, size_t bytes,
                            int64_t elapsed_ms, esp_err_t result,
                            bool full_refresh = true);
    esp_err_t ShowTestMenu(
        size_t selected,
        const std::array<ZectrixTestState,
                         static_cast<size_t>(ZectrixTestId::kCount)>& states,
        bool full_refresh);
    esp_err_t ShowTestUpdate(
        const ZectrixTestUpdate& update,
        const std::array<ZectrixTestState,
                         static_cast<size_t>(ZectrixTestId::kCount)>& states,
        bool force = false);
    esp_err_t ShowTestSummary(
        const std::array<ZectrixTestState,
                         static_cast<size_t>(ZectrixTestId::kCount)>& states);
    esp_err_t ShowDeviceInfo(const zectrix::power::PowerSnapshot& power,
                             const zectrix::system::SystemSnapshot& system,
                             bool full_refresh = true);
    esp_err_t ShowAbout(bool full_refresh = true);
    esp_err_t ShowClock(const zectrix::time::DateTime& value,
                        bool full_refresh, const char* source = "RTC",
                        bool calendar_valid = true);
    esp_err_t ShowSettings(bool auto_showcase, const char* status,
                           bool full_refresh);
    esp_err_t ShowConnectivity(const char* state, const char* status,
                               const char* passkey, size_t selected, bool full_refresh);
    esp_err_t ShowReader(const zectrix::app::ReaderController& reader, bool full_refresh);
    esp_err_t ShowBookTransfer(const zectrix::connectivity::BookTransferSnapshot& status,
                               bool choosing_mode, bool station_selected, bool full_refresh);
    esp_err_t ClearDisplay();
    esp_err_t ShowSleepCoverMenu(zectrix::app::SleepCoverStyle selected,
                                  zectrix::app::SleepCoverStyle active,
                                  const char* status, bool full_refresh);
    esp_err_t ShowSleepCover(const zectrix::app::SleepCoverSnapshot& snapshot,
                              zectrix::app::SleepCoverStyle style, bool preview = false,
                              bool preference_saved = true);

    ZectrixCanvas& canvas() { return canvas_; }
    esp_err_t RefreshFull();
    esp_err_t RefreshAuto();

private:
    void BeginContent();
    void OverlayGrayStatus();
    void DrawFrame(const char* title, const char* footer);
    static void DrawFittedText(ZectrixCanvas& canvas, int x, int y, const char* text,
                               int max_width, bool inverted = false);
    void DrawTestStrip(
        ZectrixTestId current,
        const std::array<ZectrixTestState,
                         static_cast<size_t>(ZectrixTestId::kCount)>& states);
    static const char* StateText(ZectrixTestState state);

    zectrix::display::DisplayService* display_ = nullptr;
    zectrix::time::TimeService* time_ = nullptr;
    ZectrixCanvas canvas_;
    zectrix::ui::ViewPortScheduler viewports_;
    zectrix::ui::StatusBarState status_;
    std::unique_ptr<uint8_t[]> gray_frame_;
    int64_t last_update_us_ = 0;
    bool sleep_surface_ = false;
};

#endif  // ZECTRIX_DEMO_UI_H_
