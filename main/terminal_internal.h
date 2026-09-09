#pragma once

#include <array>
#include <new>

#include "sdkconfig.h"
#include "zectrix/zectrix_sdk.h"
#include "zectrix_application_catalog.h"
#include "zectrix_demo_ui.h"
#include "zectrix_platform.h"
#include "zectrix_sleep_cover.h"

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
    class SleepCoverApplication;
    static sdk::Status CreateSleepCover(TerminalApp&, sdk::Application**);
    class ClockApplication;
    static sdk::Status CreateClock(TerminalApp&, sdk::Application**);
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

    bool AddApplication(const char* id, const char* label, Creator creator);
    bool ComposeApplications();
    void UpdateSystemStatus();
    void RunApplicationShell();
    sdk::Status Shutdown() override;
    void EnterFailsafe(sdk::Status reason) override;
    void LogHeap(const char* phase);
    ControlResult Wait(uint32_t duration_ms, bool any_click_returns);
    app::SleepCoverSnapshot ReadSleepCover();
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
#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY || CONFIG_ZECTRIX_ENABLE_READER
    zectrix::connectivity::ConnectivityService* connectivity_ = nullptr;
#endif
    ZectrixDemoUi ui_;
    ZectrixSelfTest* tests_ = nullptr;
    std::array<ZectrixTestState,
               static_cast<size_t>(ZectrixTestId::kCount)> test_states_;
    size_t launcher_selection_ = 0;
    uint32_t gallery_selection_ = 0;
#if CONFIG_ZECTRIX_ENABLE_READER
    uint32_t reader_selection_ = 0;
    bool reader_busy_ = false;
#endif
    zectrix::app::SleepCoverStyle sleep_cover_style_ = zectrix::app::kSleepCoverDefault;
    bool sleep_cover_saved_ = true;
    zectrix::power::PowerSnapshot power_snapshot_{};
    zectrix::ui::StatusBarState status_{};
    int64_t next_power_sample_us_ = 0;
    int64_t next_clock_sample_us_ = 0;
};

}  // namespace zectrix::terminal
