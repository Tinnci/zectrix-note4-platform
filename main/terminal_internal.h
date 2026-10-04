#pragma once

#include <array>
#include <new>

#include "sdkconfig.h"
#include "note4/note4_sdk.h"
#include "note4_application_catalog.h"
#include "note4_launcher_controller.h"
#include "note4_reading_overview.h"
#include "ui_engine.h"
#include "note4_platform.h"
#include "note4_health_supervisor.h"
#include "note4_sleep_cover.h"
#if CONFIG_NOTE4_ENABLE_CONNECTIVITY
#include "note4_edge_settings.h"
#endif
#if CONFIG_NOTE4_ENABLE_USB_CLI
#include "note4_cli_control.h"
#endif
#if CONFIG_NOTE4_ENABLE_UTILITIES
#include "note4_utilities.h"
#endif
#if CONFIG_NOTE4_ENABLE_USB_HOST
#include "note4_host_channel.h"
#endif

namespace note4::terminal {

namespace sdk = note4::sdk;
inline constexpr char kTag[] = "terminal";
enum class ControlResult { kContinue, kBack, kShutdown };
struct SceneResult { esp_err_t error = ESP_OK; int64_t elapsed_ms = 0; };
sdk::Status ToSdkStatus(esp_err_t result);

class TerminalApp final : public sdk::RuntimeDelegate
#if CONFIG_NOTE4_ENABLE_USB_CLI
    , public cli::MaintenanceDelegate
#endif
{
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
#if CONFIG_NOTE4_ENABLE_USB_CLI
        owner.scene_snapshot_ = {};
        owner.guest_inspection_ = {};
#endif
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
    esp_err_t PresentSleepCover(const app::SleepCoverSnapshot& snapshot, app::SleepCoverStyle style,
                               bool preview = false, bool preference_saved = true);
    app::ReadingOverview ReadReadingOverview();
    [[noreturn]] void PowerOff();
#if CONFIG_NOTE4_ENABLE_CONNECTIVITY
    void LoadEdgeConfiguration();
    bool RefreshEdgeOnWake();
    esp_err_t PresentEdgeCover();
    uint64_t NextWakeDelay();
    connectivity::EdgeSettings edge_settings_{};
    int64_t edge_next_sync_at_ = 0;
    bool edge_configuration_valid_ = false;
    bool edge_schedule_failure_ = false;
#endif
    template <typename Controller> void BindScenes(Controller& controller) {
#if CONFIG_NOTE4_ENABLE_USB_CLI
        controller.ObserveScenes(&scene_snapshot_);
#else
        (void)controller;
#endif
    }
#if CONFIG_NOTE4_ENABLE_USB_CLI
    cli::ControlStatus InspectApps(cli::ControlResult* result) override;
    cli::ControlStatus ScheduleMaintenance(cli::ControlOperation operation) override;
    cli::ControlStatus HandleDisplaySettings(const cli::ControlRequest&, cli::ControlResult*) override;
    sdk::ApplicationRuntime* runtime_ = nullptr;
    app::SceneSnapshot scene_snapshot_{};
    cli::SceneInspection guest_inspection_{};
    cli::ControlOperation maintenance_operation_ = cli::ControlOperation::kSystemInfo;
    int64_t maintenance_ready_us_ = 0;
    bool executing_maintenance_ = false;
#endif

    app::ApplicationCatalog applications_;
    std::array<Factory, app::ApplicationCatalog::kCapacity> factories_{};
    note4::Platform platform_;
    note4::input::InputService* input_ = nullptr;
    note4::power::PowerService* power_ = nullptr;
    note4::display::DisplayService* display_ = nullptr;
    note4::time::TimeService* time_ = nullptr;
    note4::storage::StorageService* storage_ = nullptr;
    note4::system::SystemService* system_ = nullptr;
#if CONFIG_NOTE4_ENABLE_USB_HOST
    host::Channel* usb_host_ = nullptr;
#endif
#if CONFIG_NOTE4_ENABLE_CONNECTIVITY || CONFIG_NOTE4_ENABLE_READER
    note4::connectivity::ConnectivityService* connectivity_ = nullptr;
#endif
    UiEngine ui_;
    Note4SelfTest* tests_ = nullptr;
    std::array<Note4TestState,
               static_cast<size_t>(Note4TestId::kCount)> test_states_;
    app::LauncherSelection launcher_selection_{};
    bool launcher_back_requested_ = false;
#if CONFIG_NOTE4_ENABLE_UTILITIES
    app::UtilitySession utilities_;
#endif
#if CONFIG_NOTE4_ENABLE_RUNTIME
    bool micro_app_busy_ = false;
#endif
    uint32_t gallery_selection_ = 0;
#if CONFIG_NOTE4_ENABLE_READER
    uint32_t reader_selection_ = 0;
    bool reader_continue_requested_ = false;
    bool reader_busy_ = false;
#endif
    note4::app::SleepCoverStyle sleep_cover_style_ = note4::app::kSleepCoverDefault;
    bool sleep_cover_saved_ = true;
    bool sleep_portrait_ = note4::app::kSleepPortraitDefault;
    bool language_saved_ = true;
    note4::power::PowerSnapshot power_snapshot_{};
    note4::ui::StatusBarState status_{};
    int64_t next_power_sample_us_ = 0;
    int64_t next_clock_sample_us_ = 0;
};

}  // namespace note4::terminal
