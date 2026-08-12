#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <new>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "zectrix_demo_ui.h"
#include "zectrix_application_runtime.h"
#include "zectrix_first_party_app_controllers.h"
#include "zectrix_display_service.h"
#include "zectrix_input_service.h"
#include "zectrix_power_service.h"
#include "zectrix_platform.h"
#include "zectrix_self_test.h"
#include "zectrix_storage_service.h"
#include "zectrix_system_service.h"
#include "zectrix_time_service.h"

extern "C" {
extern const uint8_t kLighthouse1bppStart[]
    asm("_binary_lighthouse_400x300_1bpp_bin_start");
extern const uint8_t kLighthouse1bppEnd[]
    asm("_binary_lighthouse_400x300_1bpp_bin_end");
extern const uint8_t kSnowPath1bppStart[]
    asm("_binary_snow_path_400x300_1bpp_bin_start");
extern const uint8_t kSnowPath1bppEnd[]
    asm("_binary_snow_path_400x300_1bpp_bin_end");
extern const uint8_t kFootprintAnimationStart[]
    asm("_binary_footprint_animation_bin_start");
extern const uint8_t kFootprintAnimationEnd[]
    asm("_binary_footprint_animation_bin_end");
extern const uint8_t kMountain4bppStart[]
    asm("_binary_mountain_400x300_4bpp_bin_start");
extern const uint8_t kMountain4bppEnd[]
    asm("_binary_mountain_400x300_4bpp_bin_end");
}

namespace {

constexpr char kTag[] = "epd_showcase";
constexpr uint8_t kFootprintMagic[] = {'Z', 'F', 'P', '1'};
constexpr size_t kAnimationHeaderSize = 8;
constexpr size_t kStepHeaderSize = 12;
constexpr TickType_t kHomeIdleTimeout = pdMS_TO_TICKS(15000);

enum class ControlResult {
    kContinue,
    kSelected,
    kBack,
    kShutdown,
};

struct SceneResult {
    esp_err_t error = ESP_OK;
    int64_t elapsed_ms = 0;
    ControlResult control = ControlResult::kContinue;
};

uint16_t ReadLe16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           static_cast<uint16_t>(data[1] << 8);
}

class DemoApp final : public zectrix::app::RuntimeDelegate,
                      public zectrix::app::ApplicationFactoryContext {
public:
    DemoApp() : ui_(nullptr) {
        test_states_.fill(ZectrixTestState::kWait);
    }

    void Run() {
        esp_err_t err = platform_.Initialize();
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "platform initialization failed: %s",
                     esp_err_to_name(err));
            return;
        }
        display_ = &platform_.Display();
        input_ = &platform_.Input();
        power_ = &platform_.Power();
        time_ = &platform_.Time();
        storage_ = &platform_.Storage();
        system_ = &platform_.System();
        tests_ = &platform_.Diagnostics();
        ui_.SetDisplay(display_);
        ui_.SetTime(time_);
        ESP_ERROR_CHECK(ui_.ShowSplash());
        Wait(pdMS_TO_TICKS(1500), false);

        RunApplicationShell();
    }

private:
    enum class LegacyAction : uint8_t {
        kNone,
        kAutoShowcase,
        kDisplayGallery,
        kDeviceInfo,
        kAbout,
    };

    class LauncherApplication final : public zectrix::app::Application {
    public:
        explicit LauncherApplication(DemoApp& owner) : owner_(&owner) {}

        esp_err_t Enter(zectrix::app::ApplicationContext& context) override {
            uint32_t stored = zectrix::app::kAutoShowcaseDefault;
            const esp_err_t read = owner_->storage_->GetUInt32(
                zectrix::app::kAutoShowcaseSettingKey, &stored);
            bool valid = read == ESP_OK &&
                zectrix::app::NormalizeAutoShowcaseSetting(
                    stored, &auto_showcase_);
            if (!valid) {
                auto_showcase_ = true;
                if (read == ESP_ERR_NOT_FOUND || read == ESP_OK) {
                    const esp_err_t repair = owner_->storage_->SetUInt32(
                        zectrix::app::kAutoShowcaseSettingKey,
                        zectrix::app::kAutoShowcaseDefault);
                    if (repair != ESP_OK) {
                        ESP_LOGW(kTag, "default setting save failed: %s",
                                 esp_err_to_name(repair));
                    }
                }
            }
            return context.RequestRender({0, 0, 400, 300},
                                         zectrix::app::RenderIntent::Quality)
                       ? ESP_OK : ESP_FAIL;
        }
        esp_err_t HandleEvent(const zectrix::input::InputEvent& event,
                              zectrix::app::ApplicationContext& context) override {
            const zectrix::app::LauncherResult result = controller_.Handle(event);
            if (result.decision == zectrix::app::LauncherDecision::RenderFast) {
                context.RequestRender({0, 36, 400, 234},
                                      zectrix::app::RenderIntent::Fast);
            } else if (result.decision ==
                       zectrix::app::LauncherDecision::OpenClock) {
                zectrix::app::AppCommand open;
                if (!zectrix::app::AppCommand::Open("clock", &open)) {
                    return ESP_ERR_INVALID_STATE;
                }
                context.RequestCommand(open);
            } else if (result.decision ==
                       zectrix::app::LauncherDecision::OpenSettings) {
                zectrix::app::AppCommand open;
                if (!zectrix::app::AppCommand::Open("settings", &open)) {
                    return ESP_ERR_INVALID_STATE;
                }
                context.RequestCommand(open);
            } else if (result.decision ==
                       zectrix::app::LauncherDecision::OpenDiagnostics) {
                zectrix::app::AppCommand open;
                if (!zectrix::app::AppCommand::Open("diagnostics", &open)) {
                    return ESP_ERR_INVALID_STATE;
                }
                context.RequestCommand(open);
            } else if (result.decision ==
                       zectrix::app::LauncherDecision::RunLegacy) {
                if (result.selected == 2) {
                    owner_->legacy_action_ = LegacyAction::kAutoShowcase;
                } else if (result.selected == 3) {
                    owner_->legacy_action_ = LegacyAction::kDisplayGallery;
                } else if (result.selected == 5) {
                    owner_->legacy_action_ = LegacyAction::kDeviceInfo;
                } else if (result.selected == 6) {
                    owner_->legacy_action_ = LegacyAction::kAbout;
                }
            } else if (result.decision ==
                       zectrix::app::LauncherDecision::Shutdown) {
                context.RequestCommand(zectrix::app::AppCommand::Shutdown());
            }
            return ESP_OK;
        }
        esp_err_t HandleIdle(zectrix::app::ApplicationContext&) override {
            if (auto_showcase_) {
                owner_->legacy_action_ = LegacyAction::kAutoShowcase;
            }
            return ESP_OK;
        }
        esp_err_t Render(const zectrix::app::RenderRequest& request) override {
            static constexpr const char* kItems[] = {
                "CLOCK", "SETTINGS", "AUTO SHOWCASE", "DISPLAY GALLERY",
                "HARDWARE TESTS", "DEVICE INFO", "ABOUT & LICENSE"};
            return owner_->ui_.ShowMenu(
                "ZECTRIX | LAUNCHER", kItems, std::size(kItems),
                controller_.selected(),
                "UP/DOWN Move  OK Select  Hold DOWN Off",
                request.intent == zectrix::app::RenderIntent::Quality);
        }
        esp_err_t Exit() override { return ESP_OK; }

    private:
        DemoApp* owner_;
        zectrix::app::LauncherController controller_;
        bool auto_showcase_ = true;
    };

    class ClockApplication final : public zectrix::app::Application {
    public:
        explicit ClockApplication(DemoApp& owner) : owner_(&owner) {}
        esp_err_t Enter(zectrix::app::ApplicationContext& context) override {
            return context.RequestRender({0, 0, 400, 300},
                                         zectrix::app::RenderIntent::Quality)
                       ? ESP_OK : ESP_FAIL;
        }
        esp_err_t HandleEvent(const zectrix::input::InputEvent& event,
                              zectrix::app::ApplicationContext& context) override {
            const zectrix::app::ClockDecision decision =
                zectrix::app::HandleClockInput(event);
            if (decision == zectrix::app::ClockDecision::Shutdown) {
                context.RequestCommand(zectrix::app::AppCommand::Shutdown());
            } else if (decision == zectrix::app::ClockDecision::Home) {
                context.RequestCommand(zectrix::app::AppCommand::Home());
            }
            return ESP_OK;
        }
        esp_err_t Render(const zectrix::app::RenderRequest&) override {
            zectrix::time::DateTime value;
            const esp_err_t result = owner_->time_->ReadRtc(&value);
            return result == ESP_OK ? owner_->ui_.ShowClock(value) : result;
        }
        esp_err_t Exit() override { return ESP_OK; }

    private:
        DemoApp* owner_;
    };

    class SettingsApplication final : public zectrix::app::Application {
    public:
        explicit SettingsApplication(DemoApp& owner)
            : owner_(&owner), controller_(true) {}

        esp_err_t Enter(zectrix::app::ApplicationContext& context) override {
            uint32_t stored = zectrix::app::kAutoShowcaseDefault;
            const esp_err_t read = owner_->storage_->GetUInt32(
                zectrix::app::kAutoShowcaseSettingKey, &stored);
            bool value = true;
            bool repair = false;
            if (read == ESP_ERR_NOT_FOUND) {
                status_ = "DEFAULT CREATED";
                repair = true;
            } else if (read != ESP_OK) {
                status_ = "LOAD FAILED - DEFAULT";
            } else if (!zectrix::app::NormalizeAutoShowcaseSetting(stored,
                                                                   &value)) {
                status_ = "INVALID RESET";
                value = true;
                repair = true;
            } else {
                status_ = "LOADED";
            }
            if (repair) {
                const esp_err_t save = owner_->storage_->SetUInt32(
                    zectrix::app::kAutoShowcaseSettingKey,
                    zectrix::app::kAutoShowcaseDefault);
                if (save != ESP_OK) status_ = "DEFAULT NOT SAVED";
            }
            controller_ = zectrix::app::SettingsController(value);
            return context.RequestRender({0, 0, 400, 300},
                                         zectrix::app::RenderIntent::Quality)
                       ? ESP_OK : ESP_FAIL;
        }

        esp_err_t HandleEvent(const zectrix::input::InputEvent& event,
                              zectrix::app::ApplicationContext& context) override {
            const zectrix::app::SettingsResult result = controller_.Handle(event);
            if (result.decision == zectrix::app::SettingsDecision::RenderFast) {
                status_ = "NOT SAVED";
                context.RequestRender({0, 36, 400, 234},
                                      zectrix::app::RenderIntent::Fast);
            } else if (result.decision == zectrix::app::SettingsDecision::Save) {
                const esp_err_t save = owner_->storage_->SetUInt32(
                    zectrix::app::kAutoShowcaseSettingKey,
                    result.auto_showcase ? 1 : 0);
                status_ = save == ESP_OK ? "SAVED" : "SAVE FAILED";
                context.RequestRender({0, 36, 400, 234},
                                      zectrix::app::RenderIntent::Fast);
            } else if (result.decision == zectrix::app::SettingsDecision::Home) {
                context.RequestCommand(zectrix::app::AppCommand::Home());
            } else if (result.decision ==
                       zectrix::app::SettingsDecision::Shutdown) {
                context.RequestCommand(zectrix::app::AppCommand::Shutdown());
            }
            return ESP_OK;
        }

        esp_err_t Render(const zectrix::app::RenderRequest& request) override {
            return owner_->ui_.ShowSettings(
                controller_.auto_showcase(), status_,
                request.intent == zectrix::app::RenderIntent::Quality);
        }

        esp_err_t Exit() override { return ESP_OK; }

    private:
        DemoApp* owner_;
        zectrix::app::SettingsController controller_;
        const char* status_ = "";
    };

    class DiagnosticsApplication final : public zectrix::app::Application {
    public:
        explicit DiagnosticsApplication(DemoApp& owner) : owner_(&owner) {}

        esp_err_t Enter(zectrix::app::ApplicationContext& context) override {
            owner_->test_states_.fill(ZectrixTestState::kWait);
            return context.RequestRender({0, 0, 400, 300},
                                         zectrix::app::RenderIntent::Quality)
                       ? ESP_OK : ESP_FAIL;
        }

        esp_err_t HandleEvent(const zectrix::input::InputEvent& event,
                              zectrix::app::ApplicationContext& context) override {
            const zectrix::app::DiagnosticsResult result = controller_.Handle(event);
            if (result.decision == zectrix::app::DiagnosticsDecision::RenderFast) {
                context.RequestRender({0, 36, 400, 234},
                                      zectrix::app::RenderIntent::Fast);
                return ESP_OK;
            }
            if (result.decision == zectrix::app::DiagnosticsDecision::Home) {
                context.RequestCommand(zectrix::app::AppCommand::Home());
                return ESP_OK;
            }
            if (result.decision == zectrix::app::DiagnosticsDecision::Shutdown) {
                context.RequestCommand(zectrix::app::AppCommand::Shutdown());
                return ESP_OK;
            }
            if (result.decision == zectrix::app::DiagnosticsDecision::RunAll) {
                return RunAll(context);
            }
            if (result.decision ==
                zectrix::app::DiagnosticsDecision::RunSelected) {
                return RunSelected(result.selected, context);
            }
            return ESP_OK;
        }

        esp_err_t Render(const zectrix::app::RenderRequest& request) override {
            if (controller_.page() == zectrix::app::DiagnosticsPage::Summary) {
                return owner_->ui_.ShowTestSummary(owner_->test_states_);
            }
            if (controller_.page() == zectrix::app::DiagnosticsPage::Individual) {
                return owner_->ui_.ShowTestMenu(
                    controller_.selected(), owner_->test_states_,
                    request.intent == zectrix::app::RenderIntent::Quality);
            }
            static constexpr const char* kItems[] = {
                "RUN ALL TESTS", "SELECT INDIVIDUAL TEST"};
            return owner_->ui_.ShowMenu(
                "HARDWARE TESTS", kItems, std::size(kItems),
                controller_.selected(),
                "UP/DOWN Move  OK Select  Hold OK Home",
                request.intent == zectrix::app::RenderIntent::Quality);
        }

        esp_err_t Exit() override { return ESP_OK; }

    private:
        ZectrixTestResult Execute(ZectrixTestId id) {
            owner_->test_states_[static_cast<size_t>(id)] =
                ZectrixTestState::kRunning;
            return owner_->tests_->Run(
                id, [this](const ZectrixTestUpdate& update) {
                    owner_->test_states_[static_cast<size_t>(update.id)] =
                        update.state;
                    const esp_err_t draw = owner_->ui_.ShowTestUpdate(
                        update, owner_->test_states_);
                    if (draw != ESP_OK) {
                        ESP_LOGE(kTag, "diagnostic update failed: %s",
                                 esp_err_to_name(draw));
                    }
                });
        }

        esp_err_t RunAll(zectrix::app::ApplicationContext& context) {
            owner_->test_states_.fill(ZectrixTestState::kWait);
            for (size_t index = 0;
                 index < static_cast<size_t>(ZectrixTestId::kCount); ++index) {
                esp_err_t draw = owner_->ui_.ShowTestMenu(
                    index, owner_->test_states_, true);
                if (draw != ESP_OK) return draw;
                const ZectrixTestResult result =
                    Execute(static_cast<ZectrixTestId>(index));
                if (result == ZectrixTestResult::kShutdown) {
                    context.RequestCommand(zectrix::app::AppCommand::Shutdown());
                    return ESP_OK;
                }
                if (result == ZectrixTestResult::kCancelled) break;
                owner_->test_states_[index] =
                    result == ZectrixTestResult::kPass
                        ? ZectrixTestState::kPass : ZectrixTestState::kFail;
                if (owner_->Wait(pdMS_TO_TICKS(800), false) ==
                    ControlResult::kShutdown) {
                    context.RequestCommand(zectrix::app::AppCommand::Shutdown());
                    return ESP_OK;
                }
            }
            controller_.ShowSummary();
            return owner_->ui_.ShowTestSummary(owner_->test_states_);
        }

        esp_err_t RunSelected(size_t selected,
                              zectrix::app::ApplicationContext& context) {
            const ZectrixTestResult result =
                Execute(static_cast<ZectrixTestId>(selected));
            if (result == ZectrixTestResult::kShutdown) {
                context.RequestCommand(zectrix::app::AppCommand::Shutdown());
                return ESP_OK;
            }
            if (result == ZectrixTestResult::kPass) {
                owner_->test_states_[selected] = ZectrixTestState::kPass;
            } else if (result == ZectrixTestResult::kFail) {
                owner_->test_states_[selected] = ZectrixTestState::kFail;
            }
            if (owner_->Wait(pdMS_TO_TICKS(1200), true) ==
                ControlResult::kShutdown) {
                context.RequestCommand(zectrix::app::AppCommand::Shutdown());
                return ESP_OK;
            }
            return owner_->ui_.ShowTestMenu(
                selected, owner_->test_states_, true);
        }

        DemoApp* owner_;
        zectrix::app::DiagnosticsController controller_;
    };

    static esp_err_t MakeLauncher(
        zectrix::Platform&, const zectrix::app::ApplicationRegistry&,
        zectrix::app::ApplicationFactoryContext* context,
        zectrix::app::Application** output) {
        if (context == nullptr || output == nullptr) return ESP_ERR_INVALID_ARG;
        *output = new (std::nothrow) LauncherApplication(
            *static_cast<DemoApp*>(context));
        return *output == nullptr ? ESP_ERR_NO_MEM : ESP_OK;
    }

    static esp_err_t MakeClock(
        zectrix::Platform&, const zectrix::app::ApplicationRegistry&,
        zectrix::app::ApplicationFactoryContext* context,
        zectrix::app::Application** output) {
        if (context == nullptr || output == nullptr) return ESP_ERR_INVALID_ARG;
        *output = new (std::nothrow) ClockApplication(
            *static_cast<DemoApp*>(context));
        return *output == nullptr ? ESP_ERR_NO_MEM : ESP_OK;
    }

    static esp_err_t MakeSettings(
        zectrix::Platform&, const zectrix::app::ApplicationRegistry&,
        zectrix::app::ApplicationFactoryContext* context,
        zectrix::app::Application** output) {
        if (context == nullptr || output == nullptr) return ESP_ERR_INVALID_ARG;
        *output = new (std::nothrow) SettingsApplication(
            *static_cast<DemoApp*>(context));
        return *output == nullptr ? ESP_ERR_NO_MEM : ESP_OK;
    }

    static esp_err_t MakeDiagnostics(
        zectrix::Platform&, const zectrix::app::ApplicationRegistry&,
        zectrix::app::ApplicationFactoryContext* context,
        zectrix::app::Application** output) {
        if (context == nullptr || output == nullptr) return ESP_ERR_INVALID_ARG;
        *output = new (std::nothrow) DiagnosticsApplication(
            *static_cast<DemoApp*>(context));
        return *output == nullptr ? ESP_ERR_NO_MEM : ESP_OK;
    }

    void RunApplicationShell() {
        const zectrix::app::ApplicationDescriptor descriptors[] = {
            {"launcher", "Launcher", MakeLauncher, this},
            {"clock", "Clock", MakeClock, this},
            {"settings", "Settings", MakeSettings, this},
            {"diagnostics", "Diagnostics", MakeDiagnostics, this},
        };
        while (true) {
            legacy_action_ = LegacyAction::kNone;
            {
                zectrix::app::ApplicationRuntime runtime(
                    descriptors, std::size(descriptors), "launcher", platform_,
                    *this);
                if (runtime.Start() != ESP_OK) return;
                if (runtime.Step() != ESP_OK) return;
                while (legacy_action_ == LegacyAction::kNone) {
                    zectrix::input::InputEvent event;
                    const bool received = input_->Wait(&event, kHomeIdleTimeout);
                    const esp_err_t result = received ? runtime.Step(&event)
                                                      : runtime.Idle();
                    if (result != ESP_OK) {
                        ESP_LOGE(kTag, "application step failed: %s",
                                 esp_err_to_name(result));
                    }
                    if (runtime.state() == zectrix::app::LifecycleState::Stopped ||
                        runtime.state() == zectrix::app::LifecycleState::Failsafe) {
                        return;
                    }
                }
            }
            RunLegacyAction(legacy_action_);
        }
    }

    void RunLegacyAction(LegacyAction action) {
        ControlResult result = ControlResult::kBack;
        if (action == LegacyAction::kAutoShowcase) result = RunAutoShowcase();
        else if (action == LegacyAction::kDisplayGallery) result = RunDisplayGallery();
        else if (action == LegacyAction::kDeviceInfo) result = RunDeviceInfo();
        else if (action == LegacyAction::kAbout) {
            ESP_ERROR_CHECK(ui_.ShowAbout());
            result = Wait(portMAX_DELAY, false);
        }
        if (result == ControlResult::kShutdown) PowerOff();
    }

    esp_err_t Shutdown() override {
        PowerOff();
    }

    void EnterFailsafe(esp_err_t reason) override {
        ESP_LOGE(kTag, "application runtime failsafe: %s", esp_err_to_name(reason));
    }

    ControlResult Wait(TickType_t duration, bool any_click_returns) {
        const TickType_t start = xTaskGetTickCount();
        while (xTaskGetTickCount() - start < duration) {
            zectrix::input::InputEvent event;
            const TickType_t elapsed = xTaskGetTickCount() - start;
            const TickType_t remaining = duration > elapsed ? duration - elapsed : 0;
            if (!input_->Wait(&event,
                              std::min(remaining, pdMS_TO_TICKS(100)))) {
                continue;
            }
            if (event.button == zectrix::input::Button::Down &&
                event.action == zectrix::input::Action::LongPress) {
                return ControlResult::kShutdown;
            }
            if (event.button == zectrix::input::Button::Ok &&
                event.action == zectrix::input::Action::LongPress) {
                return ControlResult::kBack;
            }
            if (any_click_returns &&
                event.action == zectrix::input::Action::Click) {
                return ControlResult::kBack;
            }
        }
        return ControlResult::kContinue;
    }

    ControlResult RunMenu(const char* title, const char* const* items,
                          size_t count, const char* footer,
                          size_t* selected, TickType_t idle_timeout = portMAX_DELAY) {
        ESP_ERROR_CHECK(ui_.ShowMenu(title, items, count, *selected, footer, true));
        while (true) {
            zectrix::input::InputEvent event;
            if (!input_->Wait(&event, idle_timeout)) {
                return ControlResult::kContinue;
            }
            if (event.button == zectrix::input::Button::Down &&
                event.action == zectrix::input::Action::LongPress) {
                return ControlResult::kShutdown;
            }
            if (event.button == zectrix::input::Button::Ok &&
                event.action == zectrix::input::Action::LongPress) {
                return ControlResult::kBack;
            }
            if (event.action != zectrix::input::Action::Click) {
                continue;
            }
            if (event.button == zectrix::input::Button::Up) {
                *selected = (*selected + count - 1) % count;
                ESP_ERROR_CHECK(ui_.ShowMenu(title, items, count, *selected,
                                             footer, false));
            } else if (event.button == zectrix::input::Button::Down) {
                *selected = (*selected + 1) % count;
                ESP_ERROR_CHECK(ui_.ShowMenu(title, items, count, *selected,
                                             footer, false));
            } else if (event.button == zectrix::input::Button::Ok) {
                return ControlResult::kSelected;
            }
        }
    }

    SceneResult RunLighthouse() {
        SceneResult result;
        const size_t size = static_cast<size_t>(kLighthouse1bppEnd -
                                                kLighthouse1bppStart);
        if (size != zectrix::display::DisplayService::kFrameBytes1Bpp) {
            result.error = ESP_ERR_INVALID_SIZE;
            return result;
        }
        const int64_t start = time_->MonotonicMicroseconds();
        result.error = display_->Present1Bpp(
            zectrix::display::DisplayIntent::Quality,
            kLighthouse1bppStart, size);
        result.elapsed_ms = (time_->MonotonicMicroseconds() - start) / 1000;
        if (result.error == ESP_OK) {
            result.control = Wait(pdMS_TO_TICKS(2500), true);
        }
        return result;
    }

    SceneResult RunFootprints() {
        SceneResult result;
        const size_t background_size = static_cast<size_t>(
            kSnowPath1bppEnd - kSnowPath1bppStart);
        const size_t animation_size = static_cast<size_t>(
            kFootprintAnimationEnd - kFootprintAnimationStart);
        if (background_size != zectrix::display::DisplayService::kFrameBytes1Bpp ||
            animation_size < kAnimationHeaderSize ||
            std::memcmp(kFootprintAnimationStart, kFootprintMagic,
                        sizeof(kFootprintMagic)) != 0) {
            result.error = ESP_ERR_INVALID_SIZE;
            return result;
        }

        const int64_t start = time_->MonotonicMicroseconds();
        result.error = display_->BeginBatch();
        if (result.error == ESP_OK) {
            result.error = display_->Present1Bpp(
                zectrix::display::DisplayIntent::FullClean,
                kSnowPath1bppStart, background_size);
        }
        if (result.error == ESP_OK) {
            result.control = Wait(pdMS_TO_TICKS(900), true);
        }

        const uint16_t count = ReadLe16(kFootprintAnimationStart + 4);
        const uint8_t* cursor = kFootprintAnimationStart + kAnimationHeaderSize;
        for (uint16_t index = 0;
             result.error == ESP_OK &&
             result.control == ControlResult::kContinue && index < count;
             ++index) {
            if (static_cast<size_t>(kFootprintAnimationEnd - cursor) <
                kStepHeaderSize) {
                result.error = ESP_ERR_INVALID_SIZE;
                break;
            }
            const uint16_t x = ReadLe16(cursor);
            const uint16_t y = ReadLe16(cursor + 2);
            const uint16_t width = ReadLe16(cursor + 4);
            const uint16_t height = ReadLe16(cursor + 6);
            const uint16_t data_size = ReadLe16(cursor + 10);
            cursor += kStepHeaderSize;
            const size_t expected = static_cast<size_t>((width + 7) / 8) * height;
            if (data_size != expected ||
                data_size > static_cast<size_t>(kFootprintAnimationEnd - cursor)) {
                result.error = ESP_ERR_INVALID_SIZE;
                break;
            }
            const zectrix::display::Rect rect = {
                static_cast<int>(x), static_cast<int>(y),
                static_cast<int>(width), static_cast<int>(height)};
            result.error = display_->Present1Bpp(
                zectrix::display::DisplayIntent::Fast,
                kSnowPath1bppStart, background_size,
                rect, cursor, data_size);
            cursor += data_size;
            if (result.error == ESP_OK) {
                result.control = Wait(pdMS_TO_TICKS(400), true);
            }
        }
        const esp_err_t end_batch = display_->EndBatch();
        if (result.error == ESP_OK) result.error = end_batch;
        result.elapsed_ms = (time_->MonotonicMicroseconds() - start) / 1000;
        if (result.error == ESP_OK &&
            result.control == ControlResult::kContinue) {
            result.control = Wait(pdMS_TO_TICKS(2200), true);
        }
        return result;
    }

    SceneResult RunMountain() {
        SceneResult result;
        const size_t size = static_cast<size_t>(kMountain4bppEnd -
                                                kMountain4bppStart);
        if (size != zectrix::display::DisplayService::kFrameBytes4Bpp) {
            result.error = ESP_ERR_INVALID_SIZE;
            return result;
        }
        const int64_t start = time_->MonotonicMicroseconds();
        // A true white 1bpp full refresh is deliberately performed before
        // every gray refresh to minimize visible history on this panel.
        result.error = ui_.ClearDisplay();
        if (result.error == ESP_OK) {
            result.error = display_->Present4Bpp(
                zectrix::display::DisplayIntent::Quality,
                kMountain4bppStart, size);
        }
        result.elapsed_ms = (time_->MonotonicMicroseconds() - start) / 1000;
        if (result.error == ESP_OK) {
            result.control = Wait(pdMS_TO_TICKS(5000), true);
        }
        return result;
    }

    SceneResult RunScene(size_t scene) {
        ESP_LOGI(kTag, "running display scene %u", static_cast<unsigned>(scene));
        if (scene == 0) return RunLighthouse();
        if (scene == 1) return RunFootprints();
        return RunMountain();
    }

    ControlResult RunAutoShowcase() {
        while (true) {
            for (size_t scene = 0; scene < 3; ++scene) {
                const SceneResult result = RunScene(scene);
                if (result.error != ESP_OK) {
                    ESP_LOGE(kTag, "scene failed: %s",
                             esp_err_to_name(result.error));
                    return ControlResult::kBack;
                }
                if (result.control != ControlResult::kContinue) {
                    return result.control;
                }
            }
        }
    }

    ControlResult RunDisplayGallery() {
        static constexpr const char* kItems[] = {
            "FULL REFRESH / 1BPP", "PARTIAL / FOOTPRINTS",
            "16-GRAY / 4BPP", "RUN ALL SCENES"};
        static constexpr const char* kTitles[] = {
            "LIGHTHOUSE", "FOOTPRINTS", "MOUNTAIN LANDSCAPE"};
        static constexpr const char* kModes[] = {
            "FULL", "PARTIAL", "FULL + PRE-CLEAR"};
        static constexpr const char* kFormats[] = {
            "1BPP B/W", "1BPP B/W", "4BPP / 16 GRAY"};
        size_t selected = 0;
        while (true) {
            const ControlResult menu = RunMenu(
                "DISPLAY GALLERY", kItems, std::size(kItems),
                "UP/DOWN Move  OK Run  Hold OK Back", &selected);
            if (menu == ControlResult::kShutdown) return menu;
            if (menu == ControlResult::kBack) {
                return ControlResult::kBack;
            }
            if (selected == 3) {
                for (size_t scene = 0; scene < 3; ++scene) {
                    const SceneResult result = RunScene(scene);
                    if (result.control == ControlResult::kShutdown) return result.control;
                    if (result.control == ControlResult::kBack) break;
                }
                continue;
            }
            const SceneResult result = RunScene(selected);
            if (result.control == ControlResult::kShutdown) return result.control;
            ESP_ERROR_CHECK(ui_.ShowSceneInfo(
                kTitles[selected], kModes[selected], kFormats[selected],
                selected == 2
                    ? zectrix::display::DisplayService::kFrameBytes4Bpp
                    : zectrix::display::DisplayService::kFrameBytes1Bpp,
                result.elapsed_ms, result.error));
            while (true) {
                zectrix::input::InputEvent event;
                if (!input_->Wait(&event, portMAX_DELAY)) continue;
                if (event.button == zectrix::input::Button::Down &&
                    event.action == zectrix::input::Action::LongPress) {
                    return ControlResult::kShutdown;
                }
                if (event.button == zectrix::input::Button::Ok &&
                    event.action == zectrix::input::Action::Click) {
                    break;
                }
                if (event.button == zectrix::input::Button::Ok &&
                    event.action == zectrix::input::Action::LongPress) {
                    break;
                }
            }
        }
    }

    ControlResult RunDeviceInfo() {
        zectrix::system::SystemSnapshot system;
        ESP_ERROR_CHECK(system_->ReadSnapshot(&system));
        ESP_ERROR_CHECK(ui_.ShowDeviceInfo(power_->ReadSnapshot(), system));
        return Wait(portMAX_DELAY, false);
    }

    [[noreturn]] void PowerOff() {
        ESP_LOGI(kTag, "clearing display before shutdown");
        const esp_err_t clear = ui_.ClearDisplay();
        if (clear != ESP_OK) {
            ESP_LOGW(kTag, "display clear failed: %s", esp_err_to_name(clear));
        }
        ESP_LOGI(kTag, "entering power service shutdown");
        power_->Shutdown();
    }

    zectrix::Platform platform_;
    zectrix::input::InputService* input_ = nullptr;
    zectrix::power::PowerService* power_ = nullptr;
    zectrix::display::DisplayService* display_ = nullptr;
    zectrix::time::TimeService* time_ = nullptr;
    zectrix::storage::StorageService* storage_ = nullptr;
    zectrix::system::SystemService* system_ = nullptr;
    ZectrixDemoUi ui_;
    ZectrixSelfTest* tests_ = nullptr;
    std::array<ZectrixTestState,
               static_cast<size_t>(ZectrixTestId::kCount)> test_states_;
    LegacyAction legacy_action_ = LegacyAction::kNone;
};

}  // namespace

extern "C" void app_main(void) {
    static DemoApp app;
    app.Run();
}
