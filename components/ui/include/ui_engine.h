#ifndef UI_ENGINE_H_
#define UI_ENGINE_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "esp_err.h"
#include "canvas.h"
#include "note4_display_service.h"
#include "note4_power_service.h"
#include "note4_self_test.h"
#include "note4_system_service.h"
#include "note4_time_service.h"
#include "status_bar.h"
#include "view_port.h"
#include "page_shell.h"

namespace note4::app { class ReaderController; }
namespace note4::app { class MicroAppController; }
namespace note4::app { class UtilityController; }
namespace note4::app { class SettingsController; }
namespace note4::app { class LauncherController; struct ReadingOverview; }
namespace note4::app { struct SleepCoverSnapshot; struct SleepCoverImage; enum class SleepCoverStyle : uint8_t; }
namespace note4::connectivity { struct BookTransferSnapshot; }
namespace note4::host { struct Snapshot; }

class UiEngine {
public:
    explicit UiEngine(note4::display::DisplayService* display);
    UiEngine(const UiEngine&) = delete;
    UiEngine& operator=(const UiEngine&) = delete;
    void SetDisplay(note4::display::DisplayService* display) {
        display_ = display;
    }
    void SetTime(note4::time::TimeService* time) { time_ = time; }
    void SetSleepPortrait(bool portrait) { sleep_portrait_ = portrait; }
    void SetDigitStyle(note4::ui::DigitStyle style) { canvas_.SetDigitStyle(style); }
    note4::ui::DigitStyle digit_style() const { return canvas_.digit_style(); }
    void UpdateStatus(const note4::ui::StatusBarState& state);
    esp_err_t RefreshPending();
    esp_err_t ShowImage1Bpp(const uint8_t* pixels, size_t size);
    esp_err_t ShowImage4Bpp(const uint8_t* pixels, size_t size);
    esp_err_t ShowImagePatch(note4::display::Rect region,
                             const uint8_t* pixels, size_t size);

    esp_err_t ShowSplash();
    esp_err_t ShowRecovery();
    esp_err_t ShowLauncher(const note4::app::LauncherController& launcher,
                           const note4::time::ClockSnapshot& clock,
                           const note4::app::ReadingOverview& reading, bool full_refresh);
    esp_err_t ShowMenu(const char* title, const char* const* items,
                       size_t count, size_t selected, const char* footer,
                       bool full_refresh);
    esp_err_t ShowSceneInfo(const char* title, const char* mode,
                            const char* format, size_t bytes,
                            int64_t elapsed_ms, esp_err_t result,
                            bool full_refresh = true);
    esp_err_t ShowTestMenu(
        size_t selected,
        const std::array<Note4TestState,
                         static_cast<size_t>(Note4TestId::kCount)>& states,
        bool full_refresh);
    esp_err_t ShowTestUpdate(
        const Note4TestUpdate& update,
        const std::array<Note4TestState,
                         static_cast<size_t>(Note4TestId::kCount)>& states,
        bool force = false);
    esp_err_t ShowTestSummary(
        const std::array<Note4TestState,
                         static_cast<size_t>(Note4TestId::kCount)>& states);
    esp_err_t ShowDeviceInfo(const note4::power::PowerSnapshot& power,
                             const note4::system::SystemSnapshot& system,
                             bool full_refresh = true);
    esp_err_t ShowAbout(bool full_refresh = true);
    esp_err_t ShowClock(const note4::time::DateTime& value,
                        bool full_refresh, const char* source = "RTC",
                        bool calendar_valid = true);
    esp_err_t ShowSettings(const note4::app::SettingsController& settings, const char* status,
                           bool full_refresh);
    esp_err_t ShowConnectivity(const char* state, const char* status,
                               const char* passkey, size_t selected, bool full_refresh);
    esp_err_t ShowReader(const note4::app::ReaderController& reader, bool full_refresh);
    esp_err_t ShowBookTransfer(const note4::connectivity::BookTransferSnapshot& status,
                               bool choosing_mode, bool station_selected, bool full_refresh);
    esp_err_t ShowUsbManager(const note4::host::Snapshot& status, bool storage_ready, bool full_refresh);
    esp_err_t ShowMicroApps(const note4::app::MicroAppController& apps, bool full_refresh);
    esp_err_t ShowUtilities(const note4::app::UtilityController& utilities, bool full_refresh);
    esp_err_t ClearDisplay();
    esp_err_t ShowSleepCoverMenu(note4::app::SleepCoverStyle selected,
                                  note4::app::SleepCoverStyle active,
                                  const char* status, bool full_refresh);
    esp_err_t ShowSleepCover(const note4::app::SleepCoverSnapshot& snapshot,
                              note4::app::SleepCoverStyle style, bool preview = false,
                              bool preference_saved = true, const note4::app::SleepCoverImage* picture = nullptr);

    Canvas& canvas() { return canvas_; }
    auto InspectViews() const { return viewports_.Inspect(); }
    esp_err_t RefreshFull();
    esp_err_t RefreshAuto();

    // Standard page shell: draws frame, status bar and adaptive footer, returning the clipped body rect.
    note4::ui::Rect BeginPage(const char* title, const char* footer, bool portrait_capable = true);
    // Enter a new page shell with fluent declarative configuration and RAII scope safety.
    note4::ui::PageShell EnterPage(const note4::ui::PageSpec& spec);

private:
    friend class note4::ui::PageShell;
    bool sleep_portrait_ = false;
    esp_err_t ShowPortraitCalendar(const note4::app::SleepCoverSnapshot& snapshot,
                                   bool preview, bool preference_saved);
    // Portrait-capable screens follow the display orientation; all others stay landscape.
    void UseCanvasMode(bool portrait);
    void ConfigureViewports();
    void BeginContent(bool portrait_capable = false);
    void OverlayGrayStatus();
    void DrawFrame(const char* title, const char* footer, bool portrait_capable = false);
    // Portrait footers use two lines, so the footer rule sits higher (360 vs 369).
    int FooterTop() const { return canvas_.height() - (canvas_.portrait() ? 40 : 31); }
    // Word/CJK wrapped text; the final allowed line is ellipsized. Returns lines drawn.
    int WrapText(int x, int y, const char* text, int max_width, int line_height, int max_lines,
                 bool center = false, bool inverted = false);
    static void DrawFittedText(Canvas& canvas, int x, int y, const char* text,
                               int max_width, bool inverted = false);
    void DrawTestStrip(
        Note4TestId current,
        const std::array<Note4TestState,
                         static_cast<size_t>(Note4TestId::kCount)>& states);
    static const char* StateText(Note4TestState state);

    note4::display::DisplayService* display_ = nullptr;
    note4::time::TimeService* time_ = nullptr;
    Canvas canvas_;
    note4::ui::ViewPortScheduler viewports_;
    note4::ui::StatusBarState status_;
    std::unique_ptr<uint8_t[]> gray_frame_;
    int64_t last_update_us_ = 0;
    bool sleep_surface_ = false;
};


#endif  // UI_ENGINE_H_
