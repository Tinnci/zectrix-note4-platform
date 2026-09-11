#pragma once

#include <array>
#include <new>

#include "sdkconfig.h"
#include "zectrix/zectrix_sdk.h"
#include "zectrix_application_catalog.h"
#include "zectrix_launcher_controller.h"
#include "zectrix_reading_overview.h"
#include "zectrix_demo_ui.h"
#include "zectrix_platform.h"
#include "zectrix_sleep_cover.h"
#if CONFIG_ZECTRIX_ENABLE_UTILITIES
#include "zectrix_utilities.h"
#endif
#if CONFIG_ZECTRIX_ENABLE_USB_HOST
#include "zectrix_host_channel.h"
#endif

namespace zectrix::terminal {

namespace sdk = zectrix::sdk;
inline constexpr char kTag[] = "terminal";
enum class ControlResult { kContinue, kBack, kShutdown };
struct SceneResult { esp_err_t error = ESP_OK; int64_t elapsed_ms = 0; };
sdk::Status ToSdkStatus(esp_err_t result);

class TerminalApp final : public sdk::RuntimeDelegate {
public:
    TerminalApp();
    void Run();

private:
    using Creator = sdk::Status (*)(TerminalApp&, sdk::Application**);
    class Factory final : public sdk::ApplicationFactory {
    public:
        TerminalApp* owner = nullptr;
        Creator creator = nullptr;
        sdk::Status Create(const sdk::ApplicationRegistry&, sdk::Application** output) override {
            return creator(*owner, output);
        }
    };
    template <typename ApplicationType>
    static sdk::Status CreateApplication(TerminalApp& owner, sdk::Application** output) {
        if (!output) return sdk::Status::InvalidArgument;
        *output = new (std::nothrow) ApplicationType(owner);
        return *output ? sdk::Status::Ok : sdk::Status::NoMemory;
    }
    class LauncherApplication;
    static sdk::Status CreateLauncher(TerminalApp&, sdk::Application**);
    class ReaderApplication;
    static sdk::Status CreateReader(TerminalApp&, sdk::Application**);
    class BookTransferApplication;
    static sdk::Status CreateBookTransfer(TerminalApp&, sdk::Application**);
    class UsbManagerApplication;
    static sdk::Status CreateUsbManager(TerminalApp&, sdk::Application**);
    class MicroAppsApplication;
    static sdk::Status CreateMicroApps(TerminalApp&, sdk::Application**);
    class SleepCoverApplication;
    static sdk::Status CreateSleepCover(TerminalApp&, sdk::Application**);
    class ClockApplication;
    static sdk::Status CreateClock(TerminalApp&, sdk::Application**);
    class UtilitiesApplication;
    static sdk::Status CreateUtilities(TerminalApp&, sdk::Application**);
    class SettingsApplication;
    static sdk::Status CreateSettings(TerminalApp&, sdk::Application**);
    class ConnectivityApplication;
    static sdk::Status CreateConnectivity(TerminalApp&, sdk::Application**);
    class DiagnosticsApplication;
    static sdk::Status CreateDiagnostics(TerminalApp&, sdk::Application**);
    class GalleryApplication;
    static sdk::Status CreateGallery(TerminalApp&, sdk::Application**);
    class ShowcaseApplication;
    static sdk::Status CreateShowcase(TerminalApp&, sdk::Application**);
    class DeviceInfoApplication;
    static sdk::Status CreateDeviceInfo(TerminalApp&, sdk::Application**);
    class AboutApplication;
    static sdk::Status CreateAbout(TerminalApp&, sdk::Application**);

    bool AddApplication(const char* id, const char* label, Creator creator,
                        app::ApplicationPresentation presentation = {});
    bool ComposeApplications();
    void UpdateSystemStatus();
    void RunApplicationShell();
    sdk::Status RequestBack(sdk::ApplicationContext& context);
    sdk::Status Shutdown() override;
    void EnterFailsafe(sdk::Status reason) override;
    void LogHeap(const char* phase);
    ControlResult Wait(uint32_t duration_ms, bool confirm_returns);
    app::SleepCoverSnapshot ReadSleepCover();
    app::ReadingOverview ReadReadingOverview();
    [[noreturn]] void PowerOff();

    app::ApplicationCatalog applications_;
    std::array<Factory, app::ApplicationCatalog::kCapacity> factories_{};
    zectrix::Platform platform_;
    zectrix::input::InputService* input_ = nullptr;
    zectrix::power::PowerService* power_ = nullptr;
    zectrix::display::DisplayService* display_ = nullptr;
    zectrix::time::TimeService* time_ = nullptr;
    zectrix::storage::StorageService* storage_ = nullptr;
    zectrix::system::SystemService* system_ = nullptr;
#if CONFIG_ZECTRIX_ENABLE_USB_HOST
    host::Channel* usb_host_ = nullptr;
#endif
#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY || CONFIG_ZECTRIX_ENABLE_READER
    zectrix::connectivity::ConnectivityService* connectivity_ = nullptr;
#endif
    ZectrixDemoUi ui_;
    ZectrixSelfTest* tests_ = nullptr;
    std::array<ZectrixTestState,
               static_cast<size_t>(ZectrixTestId::kCount)> test_states_;
    app::LauncherSelection launcher_selection_{};
    bool launcher_back_requested_ = false;
#if CONFIG_ZECTRIX_ENABLE_UTILITIES
    app::UtilitySession utilities_;
#endif
#if CONFIG_ZECTRIX_ENABLE_RUNTIME
    bool micro_app_busy_ = false;
#endif
    uint32_t gallery_selection_ = 0;
#if CONFIG_ZECTRIX_ENABLE_READER
    uint32_t reader_selection_ = 0;
    bool reader_continue_requested_ = false;
    bool reader_busy_ = false;
#endif
    zectrix::app::SleepCoverStyle sleep_cover_style_ = zectrix::app::kSleepCoverDefault;
    bool sleep_cover_saved_ = true;
    bool language_saved_ = true;
    zectrix::power::PowerSnapshot power_snapshot_{};
    zectrix::ui::StatusBarState status_{};
    int64_t next_power_sample_us_ = 0;
    int64_t next_clock_sample_us_ = 0;
};

}  // namespace zectrix::terminal
