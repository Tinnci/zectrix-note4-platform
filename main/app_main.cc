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
#include "zectrix/zectrix_sdk.h"
#include "zectrix_connectivity_service.h"
#include "zectrix_demo_ui.h"
#include "zectrix_first_party_app_controllers.h"
#include "zectrix_scene_manager.h"
#include "zectrix_gallery_controller.h"
#include "zectrix_display_service.h"
#include "zectrix_input_service.h"
#include "zectrix_power_service.h"
#include "zectrix_platform.h"
#include "zectrix_self_test.h"
#include "zectrix_storage_service.h"
#include "zectrix_system_service.h"
#include "zectrix_time_service.h"
#include "zectrix_update_service.h"

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

namespace sdk = zectrix::sdk;

constexpr char kTag[] = "terminal";
constexpr uint8_t kFootprintMagic[] = {'Z', 'F', 'P', '1'};
constexpr size_t kAnimationHeaderSize = 8;
constexpr size_t kStepHeaderSize = 12;
constexpr TickType_t kOwnerPollTimeout = pdMS_TO_TICKS(250);
constexpr int64_t kHomeIdleTimeoutUs = 15000000;

enum class ControlResult {
    kContinue,
    kBack,
    kShutdown,
};

struct SceneResult {
    esp_err_t error = ESP_OK;
    int64_t elapsed_ms = 0;
};

sdk::Status ToSdkStatus(esp_err_t result) {
    switch (result) {
        case ESP_OK: return sdk::Status::Ok;
        case ESP_ERR_INVALID_ARG: return sdk::Status::InvalidArgument;
        case ESP_ERR_INVALID_STATE: return sdk::Status::InvalidState;
        case ESP_ERR_NOT_FOUND: return sdk::Status::NotFound;
        case ESP_ERR_NO_MEM: return sdk::Status::NoMemory;
        case ESP_ERR_TIMEOUT: return sdk::Status::Timeout;
        case ESP_ERR_NOT_SUPPORTED: return sdk::Status::Unsupported;
        default: return sdk::Status::IoError;
    }
}

uint16_t ReadLe16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           static_cast<uint16_t>(data[1] << 8);
}

class TerminalApp final : public sdk::RuntimeDelegate {
public:
    TerminalApp() : ui_(nullptr) {
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
        connectivity_ = &platform_.Connectivity();
        connectivity_->UpdatePower(power_->ReadSnapshot());
        int32_t rtc_utc_offset = 0;
        if (storage_->GetInt32("rtc_utc_offset", &rtc_utc_offset) == ESP_OK &&
            time_->SynchronizeSystemClockFromRtc(rtc_utc_offset) != ESP_OK) {
            ESP_LOGW(kTag, "RTC could not initialize the HTTPS validation clock");
        }
        tests_ = &platform_.Diagnostics();
        LogHeap("M2-equivalent platform");
        ui_.SetDisplay(display_);
        ui_.SetTime(time_);
        UpdateSystemStatus();
        ESP_ERROR_CHECK(ui_.ShowSplash());
        Wait(pdMS_TO_TICKS(1500), false);

        RunApplicationShell();
    }

private:
    template <typename ApplicationType>
    class OwnedFactory final : public sdk::ApplicationFactory {
    public:
        explicit OwnedFactory(TerminalApp& owner) : owner_(&owner) {}

        sdk::Status Create(const sdk::ApplicationRegistry&,
                           sdk::Application** output) override {
            if (output == nullptr) return sdk::Status::InvalidArgument;
            *output = new (std::nothrow) ApplicationType(*owner_);
            return *output == nullptr ? sdk::Status::NoMemory : sdk::Status::Ok;
        }

    private:
        TerminalApp* owner_;
    };

    class LauncherApplication final : public sdk::Application {
    public:
        explicit LauncherApplication(TerminalApp& owner)
            : owner_(&owner), controller_(owner.launcher_selection_) {}

        sdk::Status Enter(sdk::ApplicationContext& context) override {
            uint32_t stored = zectrix::app::kAutoShowcaseDefault;
            const esp_err_t read = owner_->storage_->GetUInt32(
                zectrix::app::kAutoShowcaseSettingKey, &stored);
            bool valid = read == ESP_OK &&
                zectrix::app::NormalizeAutoShowcaseSetting(
                    stored, &auto_showcase_);
            if (!valid) {
                auto_showcase_ = zectrix::app::kAutoShowcaseDefault != 0;
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
            last_input_us_ = owner_->time_->MonotonicMicroseconds();
            return context.RequestRender({0, 0, 400, 300},
                                         sdk::RenderIntent::Quality)
                       ? sdk::Status::Ok : sdk::Status::InternalError;
        }
        sdk::Status HandleEvent(const sdk::InputEvent& event,
                              sdk::ApplicationContext& context) override {
            last_input_us_ = owner_->time_->MonotonicMicroseconds();
            const zectrix::app::LauncherResult result = controller_.Handle(event);
            using Decision = zectrix::app::LauncherDecision;
            const char* target = nullptr;
            switch (result.decision) {
                case Decision::RenderFast:
                    context.RequestRender({0, 24, 400, 276}, sdk::RenderIntent::Fast);
                    break;
                case Decision::OpenClock: target = "clock"; break;
                case Decision::OpenSettings: target = "settings"; break;
                case Decision::OpenConnectivity: target = "connectivity"; break;
                case Decision::OpenDiagnostics: target = "diagnostics"; break;
                case Decision::OpenShowcase: target = "showcase"; break;
                case Decision::OpenGallery: target = "gallery"; break;
                case Decision::OpenDeviceInfo: target = "device-info"; break;
                case Decision::OpenAbout: target = "about"; break;
                case Decision::Shutdown:
                    context.RequestCommand(sdk::AppCommand::Shutdown());
                    break;
                case Decision::None: break;
            }
            if (target) {
                sdk::AppCommand open;
                if (!sdk::AppCommand::Open(target, &open)) return sdk::Status::InvalidState;
                context.RequestCommand(open);
            }
            return sdk::Status::Ok;
        }
        sdk::Status HandleIdle(sdk::ApplicationContext& context) override {
            const int64_t now = owner_->time_->MonotonicMicroseconds();
            if (auto_showcase_ && now - last_input_us_ >= kHomeIdleTimeoutUs) {
                last_input_us_ = now;
                sdk::AppCommand open;
                if (sdk::AppCommand::Open("showcase", &open)) context.RequestCommand(open);
            }
            return sdk::Status::Ok;
        }
        sdk::Status Render(const sdk::RenderRequest& request) override {
            static constexpr const char* kItems[] = {
                "CLOCK", "SETTINGS", "CONNECTIVITY", "AUTO SHOWCASE",
                "DISPLAY GALLERY", "HARDWARE TESTS", "DEVICE INFO",
                "ABOUT & LICENSE"};
            return ToSdkStatus(owner_->ui_.ShowMenu(
                "ZECTRIX | LAUNCHER", kItems, std::size(kItems),
                controller_.selected(),
                "UP/DOWN Move  OK Select  Hold DOWN Off",
                request.intent == sdk::RenderIntent::Quality));
        }
        sdk::Status Exit() override {
            owner_->launcher_selection_ = controller_.selected();
            return sdk::Status::Ok;
        }

    private:
        TerminalApp* owner_;
        zectrix::app::LauncherController controller_;
        bool auto_showcase_ = false;
        int64_t last_input_us_ = 0;
    };

    class ClockApplication final : public sdk::Application {
    public:
        explicit ClockApplication(TerminalApp& owner) : owner_(&owner) {}
        sdk::Status Enter(sdk::ApplicationContext& context) override {
            ReadTime();
            return context.RequestRender({0, 0, 400, 300},
                                         sdk::RenderIntent::Quality)
                       ? sdk::Status::Ok : sdk::Status::InternalError;
        }
        sdk::Status HandleEvent(const sdk::InputEvent& event,
                              sdk::ApplicationContext& context) override {
            const zectrix::app::ClockDecision decision =
                zectrix::app::HandleClockInput(event);
            if (decision == zectrix::app::ClockDecision::Shutdown) {
                context.RequestCommand(sdk::AppCommand::Shutdown());
            } else if (decision == zectrix::app::ClockDecision::Home) {
                context.RequestCommand(sdk::AppCommand::Home());
            }
            return sdk::Status::Ok;
        }
        sdk::Status HandleIdle(sdk::ApplicationContext& context) override {
            if (owner_->time_->MonotonicMicroseconds() < next_read_us_) return sdk::Status::Ok;
            const zectrix::app::ClockMinute displayed{
                value_.year, value_.month, value_.day,
                value_.hour, value_.minute};
            const auto previous_source = source_;
            ReadTime();
            const zectrix::app::ClockMinute next{
                value_.year, value_.month, value_.day,
                value_.hour, value_.minute};
            const bool changed =
                zectrix::app::ClockDisplayChanged(displayed, next) || previous_source != source_;
            if (changed) {
                context.RequestRender({0, 24, 400, 276},
                                      sdk::RenderIntent::Fast);
            }
            return sdk::Status::Ok;
        }
        sdk::Status Render(const sdk::RenderRequest& request) override {
            return ToSdkStatus(owner_->ui_.ShowClock(
                value_, request.intent == sdk::RenderIntent::Quality,
                source_ == zectrix::time::ClockSource::Rtc ? "RTC" :
                source_ == zectrix::time::ClockSource::System ? "SYSTEM TIME" : "UPTIME (HH:MM)",
                source_ != zectrix::time::ClockSource::Uptime));
        }
        sdk::Status Exit() override { return sdk::Status::Ok; }

    private:
        void ReadTime() {
            const esp_err_t read = owner_->time_->ReadRtc(&value_);
            if (read == ESP_OK) {
                source_ = zectrix::time::ClockSource::Rtc;
            } else {
                const auto fallback = owner_->time_->Now();
                value_ = fallback.value;
                if (!sampled_ || source_ == zectrix::time::ClockSource::Rtc) {
                    ESP_LOGW(kTag, "clock RTC unavailable: %s; using %s", esp_err_to_name(read),
                             fallback.source == zectrix::time::ClockSource::System ? "system time" : "uptime");
                }
                source_ = fallback.source;
            }
            sampled_ = true;
            next_read_us_ = owner_->time_->MonotonicMicroseconds() + 1000000;
        }

        TerminalApp* owner_;
        zectrix::time::DateTime value_{};
        zectrix::time::ClockSource source_ = zectrix::time::ClockSource::Uptime;
        bool sampled_ = false;
        int64_t next_read_us_ = 0;
    };

    class SettingsApplication final : public sdk::Application {
    public:
        explicit SettingsApplication(TerminalApp& owner)
            : owner_(&owner), controller_(zectrix::app::kAutoShowcaseDefault != 0) {}

        sdk::Status Enter(sdk::ApplicationContext& context) override {
            uint32_t stored = zectrix::app::kAutoShowcaseDefault;
            const esp_err_t read = owner_->storage_->GetUInt32(
                zectrix::app::kAutoShowcaseSettingKey, &stored);
            bool value = zectrix::app::kAutoShowcaseDefault != 0;
            bool repair = false;
            if (read == ESP_ERR_NOT_FOUND) {
                status_ = "DEFAULT CREATED";
                repair = true;
            } else if (read != ESP_OK) {
                status_ = "LOAD FAILED - DEFAULT";
            } else if (!zectrix::app::NormalizeAutoShowcaseSetting(stored,
                                                                   &value)) {
                status_ = "INVALID RESET";
                value = zectrix::app::kAutoShowcaseDefault != 0;
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
                                         sdk::RenderIntent::Quality)
                       ? sdk::Status::Ok : sdk::Status::InternalError;
        }

        sdk::Status HandleEvent(const sdk::InputEvent& event,
                              sdk::ApplicationContext& context) override {
            const zectrix::app::SettingsResult result = controller_.Handle(event);
            if (result.decision == zectrix::app::SettingsDecision::RenderFast) {
                status_ = "NOT SAVED";
                context.RequestRender({0, 24, 400, 276},
                                      sdk::RenderIntent::Fast);
            } else if (result.decision == zectrix::app::SettingsDecision::Save) {
                const esp_err_t save = owner_->storage_->SetUInt32(
                    zectrix::app::kAutoShowcaseSettingKey,
                    result.auto_showcase ? 1 : 0);
                status_ = save == ESP_OK ? "SAVED" : "SAVE FAILED";
                context.RequestRender({0, 24, 400, 276},
                                      sdk::RenderIntent::Fast);
            } else if (result.decision == zectrix::app::SettingsDecision::Home) {
                context.RequestCommand(sdk::AppCommand::Home());
            } else if (result.decision ==
                       zectrix::app::SettingsDecision::Shutdown) {
                context.RequestCommand(sdk::AppCommand::Shutdown());
            }
            return sdk::Status::Ok;
        }

        sdk::Status Render(const sdk::RenderRequest& request) override {
            return ToSdkStatus(owner_->ui_.ShowSettings(
                controller_.auto_showcase(), status_,
                request.intent == sdk::RenderIntent::Quality));
        }

        sdk::Status Exit() override { return sdk::Status::Ok; }

    private:
        TerminalApp* owner_;
        zectrix::app::SettingsController controller_;
        const char* status_ = "";
    };

    class ConnectivityApplication final : public sdk::Application {
    public:
        explicit ConnectivityApplication(TerminalApp& owner) : owner_(&owner) {}

        sdk::Status Enter(sdk::ApplicationContext& context) override {
            status_ = "PRESS OK TO PAIR A NEW PHONE";
            passkey_[0] = '\0';
            displayed_state_ = owner_->connectivity_->State();
            return context.RequestRender({0, 0, 400, 300},
                                         sdk::RenderIntent::Quality)
                       ? sdk::Status::Ok : sdk::Status::InternalError;
        }

        sdk::Status HandleEvent(const sdk::InputEvent& event,
                                sdk::ApplicationContext& context) override {
            const auto decision =
                zectrix::app::HandleConnectivityInput(event);
            if (decision == zectrix::app::ConnectivityDecision::Home) {
                context.RequestCommand(sdk::AppCommand::Home());
            } else if (decision ==
                       zectrix::app::ConnectivityDecision::Shutdown) {
                context.RequestCommand(sdk::AppCommand::Shutdown());
            } else if (decision ==
                       zectrix::app::ConnectivityDecision::StartPairing) {
                const auto result = owner_->connectivity_->StartLocalPairing();
                status_ = result == zectrix::connectivity::ConnectivityResult::kOk
                              ? "PHONE CAN PAIR FOR 120 SECONDS"
                              : "PAIRING IS NOT AVAILABLE";
                context.RequestRender({0, 24, 400, 276},
                                      sdk::RenderIntent::Fast);
            } else if (decision ==
                       zectrix::app::ConnectivityDecision::FetchResource) {
                zectrix::companion::ResourceRequestMessage request{};
                owner_->connectivity_->UpdatePower(owner_->power_->ReadSnapshot());
                const auto result =
                    owner_->connectivity_->RequestResource(request);
                if (result ==
                    zectrix::connectivity::ConnectivityResult::kOk) {
                    status_ = "REQUESTING TEST DOCUMENT";
                } else if (result ==
                           zectrix::connectivity::ConnectivityResult::kBusy) {
                    status_ = "RESOURCE REQUEST ALREADY ACTIVE";
                } else {
                    status_ = "RESOURCE SERVICE UNAVAILABLE";
                }
                context.RequestRender({0, 24, 400, 276},
                                      sdk::RenderIntent::Fast);
            } else if (decision ==
                       zectrix::app::ConnectivityDecision::ClearBonds) {
                const auto result = owner_->connectivity_->ClearPeerBonds();
                status_ = result == zectrix::connectivity::ConnectivityResult::kOk
                              ? "TRUSTED PHONE FORGOTTEN"
                              : "DISCONNECT BEFORE FORGETTING";
                context.RequestRender({0, 24, 400, 276},
                                      sdk::RenderIntent::Fast);
            }
            return sdk::Status::Ok;
        }

        sdk::Status HandleIdle(sdk::ApplicationContext& context) override {
            bool changed = false;
            zectrix::connectivity::ResourceResponse response{};
            if (owner_->connectivity_->TakeResourceResponse(&response)) {
                SetResourceStatus(response);
                changed = true;
            }
            uint32_t passkey = 0;
            if (owner_->connectivity_->TakePairingPasskey(&passkey)) {
                std::snprintf(passkey_, sizeof(passkey_), "%06lu",
                              static_cast<unsigned long>(passkey));
                status_ = "ENTER ON PHONE";
                changed = true;
            }
            const auto current_state = owner_->connectivity_->State();
            if (current_state != displayed_state_) {
                displayed_state_ = current_state;
                if (current_state ==
                        zectrix::connectivity::ConnectivityState::kSecure ||
                    current_state ==
                        zectrix::connectivity::ConnectivityState::kLinkReady ||
                    current_state == zectrix::connectivity::
                        ConnectivityState::kProtocolNegotiatedLocal) {
                    std::memset(passkey_, 0, sizeof(passkey_));
                    status_ = current_state == zectrix::connectivity::
                                      ConnectivityState::kProtocolNegotiatedLocal
                                  ? "HELLO ACCEPTED; AUTHORIZATION NEXT"
                                  : current_state ==
                                      zectrix::connectivity::ConnectivityState::kLinkReady
                                  ? "SECURE BLE READY; VERIFYING PHONE"
                                  : "LINK SECURED; ENABLING UPDATES";
                } else if (current_state ==
                           zectrix::connectivity::ConnectivityState::kPairing) {
                    status_ = "SELECT ZECTRIX NOTE4 ON PHONE";
                } else if (current_state ==
                           zectrix::connectivity::ConnectivityState::kAdvertising) {
                    status_ = "TRUSTED PHONE CAN RECONNECT";
                } else if (current_state ==
                           zectrix::connectivity::ConnectivityState::kFault) {
                    status_ = "BLUETOOTH NEEDS A RESTART";
                }
                changed = true;
            }
            if (changed) {
                context.RequestRender({0, 24, 400, 276},
                                      sdk::RenderIntent::Fast);
            }
            return sdk::Status::Ok;
        }

        sdk::Status Render(const sdk::RenderRequest& request) override {
            return ToSdkStatus(owner_->ui_.ShowConnectivity(
                StateText(owner_->connectivity_->State()), status_,
                passkey_[0] == '\0' ? nullptr : passkey_,
                request.intent == sdk::RenderIntent::Quality));
        }

        sdk::Status Exit() override {
            std::memset(passkey_, 0, sizeof(passkey_));
            return sdk::Status::Ok;
        }

    private:
        void SetResourceStatus(
            const zectrix::connectivity::ResourceResponse& response) {
            using Status = zectrix::companion::ResourceStatus;
            switch (response.status) {
                case Status::kSuccess:
                    std::snprintf(
                        resource_status_, sizeof(resource_status_),
                        "FETCHED %u BYTES VIA %s",
                        static_cast<unsigned>(response.body_size),
                        response.path == zectrix::companion::ConnectivityPath::kDirectWifi
                            ? "WI-FI" : "PHONE");
                    break;
                case Status::kPhoneUnavailable:
                    std::snprintf(resource_status_, sizeof(resource_status_),
                                  "%s", response.retry_queued
                                      ? "NETWORK UNAVAILABLE; RETRY QUEUED"
                                      : "NETWORK RESOURCE UNAVAILABLE");
                    break;
                case Status::kPhoneOffline:
                    std::snprintf(resource_status_, sizeof(resource_status_),
                                  "%s", response.retry_queued
                                      ? "PHONE OFFLINE; RETRY QUEUED" : "PHONE OFFLINE");
                    break;
                case Status::kTimeout:
                    std::snprintf(resource_status_, sizeof(resource_status_),
                                  "%s", response.retry_queued
                                      ? "REQUEST TIMED OUT; RETRY QUEUED" : "REQUEST TIMED OUT");
                    break;
                case Status::kServerError:
                    std::snprintf(resource_status_, sizeof(resource_status_),
                                  "RESOURCE SERVER ERROR");
                    break;
                case Status::kResponseTooLarge:
                    std::snprintf(resource_status_, sizeof(resource_status_),
                                  "RESOURCE RESPONSE TOO LARGE");
                    break;
                case Status::kNotAuthorized:
                    std::snprintf(resource_status_, sizeof(resource_status_),
                                  "%s", response.path ==
                                      zectrix::companion::ConnectivityPath::kDirectWifi
                                      ? "WI-FI AUTHENTICATION FAILED"
                                      : "PHONE AUTHORIZATION REQUIRED");
                    break;
                case Status::kUnsupportedCapability:
                    std::snprintf(resource_status_, sizeof(resource_status_),
                                  "RESOURCE NOT SUPPORTED");
                    break;
                default:
                    std::snprintf(resource_status_, sizeof(resource_status_),
                                  "INVALID RESOURCE RESPONSE");
                    break;
            }
            if (response.wifi_stop == zectrix::connectivity::WifiStopResult::kFailure) {
                std::snprintf(resource_status_, sizeof(resource_status_),
                              "WI-FI STOP FAILED; RESTART REQUIRED");
            }
            status_ = resource_status_;
        }

        static const char* StateText(
            zectrix::connectivity::ConnectivityState state) {
            switch (state) {
                case zectrix::connectivity::ConnectivityState::kIdle: return "OFFLINE";
                case zectrix::connectivity::ConnectivityState::kAdvertising:
                    return "READY TO RECONNECT";
                case zectrix::connectivity::ConnectivityState::kPairing:
                    return "PAIRING OPEN";
                case zectrix::connectivity::ConnectivityState::kSecuring:
                    return "SECURING LINK";
                case zectrix::connectivity::ConnectivityState::kSecure:
                    return "SECURE LINK";
                case zectrix::connectivity::ConnectivityState::kLinkReady:
                    return "BLE READY";
                case zectrix::connectivity::ConnectivityState::kProtocolNegotiatedLocal:
                    return "PROTOCOL NEGOTIATED";
                case zectrix::connectivity::ConnectivityState::kFault: return "FAULT";
                default: return "STOPPED";
            }
        }

        TerminalApp* owner_;
        const char* status_ = "";
        char resource_status_[48]{};
        char passkey_[7]{};
        zectrix::connectivity::ConnectivityState displayed_state_ =
            zectrix::connectivity::ConnectivityState::kStopped;
    };

    class DiagnosticsApplication final : public sdk::Application {
    public:
        explicit DiagnosticsApplication(TerminalApp& owner) : owner_(&owner) {}

        sdk::Status Enter(sdk::ApplicationContext& context) override {
            owner_->test_states_.fill(ZectrixTestState::kWait);
            return context.RequestRender({0, 0, 400, 300},
                                         sdk::RenderIntent::Quality)
                       ? sdk::Status::Ok : sdk::Status::InternalError;
        }

        sdk::Status HandleEvent(const sdk::InputEvent& event,
                              sdk::ApplicationContext& context) override {
            const zectrix::app::DiagnosticsResult result = controller_.Handle(event);
            if (result.decision == zectrix::app::DiagnosticsDecision::RenderFast) {
                context.RequestRender({0, 24, 400, 276},
                                      sdk::RenderIntent::Fast);
                return sdk::Status::Ok;
            }
            if (result.decision == zectrix::app::DiagnosticsDecision::Home) {
                context.RequestCommand(sdk::AppCommand::Home());
                return sdk::Status::Ok;
            }
            if (result.decision == zectrix::app::DiagnosticsDecision::Shutdown) {
                context.RequestCommand(sdk::AppCommand::Shutdown());
                return sdk::Status::Ok;
            }
            if (result.decision == zectrix::app::DiagnosticsDecision::RunAll) {
                return RunAll(context);
            }
            if (result.decision ==
                zectrix::app::DiagnosticsDecision::RunSelected) {
                return RunSelected(result.selected, context);
            }
            return sdk::Status::Ok;
        }

        sdk::Status Render(const sdk::RenderRequest& request) override {
            if (controller_.page() == zectrix::app::DiagnosticsPage::Summary) {
                return ToSdkStatus(
                    owner_->ui_.ShowTestSummary(owner_->test_states_));
            }
            if (controller_.page() == zectrix::app::DiagnosticsPage::Individual) {
                return ToSdkStatus(owner_->ui_.ShowTestMenu(
                    controller_.selected(), owner_->test_states_,
                    request.intent == sdk::RenderIntent::Quality));
            }
            static constexpr const char* kItems[] = {
                "RUN ALL TESTS", "SELECT INDIVIDUAL TEST"};
            return ToSdkStatus(owner_->ui_.ShowMenu(
                "HARDWARE TESTS", kItems, std::size(kItems),
                controller_.selected(),
                "UP/DOWN Move  OK Select  Hold OK Home",
                request.intent == sdk::RenderIntent::Quality));
        }

        sdk::Status Exit() override { return sdk::Status::Ok; }

    private:
        ZectrixTestResult Execute(ZectrixTestId id) {
            owner_->test_states_[static_cast<size_t>(id)] =
                ZectrixTestState::kRunning;
            return owner_->tests_->Run(
                id, [this](const ZectrixTestUpdate& update) {
                    owner_->UpdateSystemStatus();
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

        sdk::Status RunAll(sdk::ApplicationContext& context) {
            owner_->test_states_.fill(ZectrixTestState::kWait);
            for (size_t index = 0;
                 index < static_cast<size_t>(ZectrixTestId::kCount); ++index) {
                esp_err_t draw = owner_->ui_.ShowTestMenu(
                    index, owner_->test_states_, true);
                if (draw != ESP_OK) return ToSdkStatus(draw);
                const ZectrixTestResult result =
                    Execute(static_cast<ZectrixTestId>(index));
                if (result == ZectrixTestResult::kShutdown) {
                    context.RequestCommand(sdk::AppCommand::Shutdown());
                    return sdk::Status::Ok;
                }
                if (result == ZectrixTestResult::kCancelled) break;
                owner_->test_states_[index] =
                    result == ZectrixTestResult::kPass
                        ? ZectrixTestState::kPass : ZectrixTestState::kFail;
                if (owner_->Wait(pdMS_TO_TICKS(800), false) ==
                    ControlResult::kShutdown) {
                    context.RequestCommand(sdk::AppCommand::Shutdown());
                    return sdk::Status::Ok;
                }
            }
            controller_.ShowSummary();
            return ToSdkStatus(
                owner_->ui_.ShowTestSummary(owner_->test_states_));
        }

        sdk::Status RunSelected(size_t selected,
                              sdk::ApplicationContext& context) {
            const ZectrixTestResult result =
                Execute(static_cast<ZectrixTestId>(selected));
            if (result == ZectrixTestResult::kShutdown) {
                context.RequestCommand(sdk::AppCommand::Shutdown());
                return sdk::Status::Ok;
            }
            if (result == ZectrixTestResult::kPass) {
                owner_->test_states_[selected] = ZectrixTestState::kPass;
            } else if (result == ZectrixTestResult::kFail) {
                owner_->test_states_[selected] = ZectrixTestState::kFail;
            }
            if (owner_->Wait(pdMS_TO_TICKS(1200), true) ==
                ControlResult::kShutdown) {
                context.RequestCommand(sdk::AppCommand::Shutdown());
                return sdk::Status::Ok;
            }
            return ToSdkStatus(owner_->ui_.ShowTestMenu(
                selected, owner_->test_states_, true));
        }

        TerminalApp* owner_;
        zectrix::app::DiagnosticsController controller_;
    };

    class GalleryApplication : public sdk::Application {
    public:
        explicit GalleryApplication(TerminalApp& owner, bool automatic = false)
            : owner_(&owner), controller_(automatic, owner.gallery_selection_) {}

        sdk::Status Enter(sdk::ApplicationContext& context) override {
            const auto result = controller_.Start();
            if (!sdk::IsOk(result)) return result;
            context.RequestRender({0, 24, 400, 276}, sdk::RenderIntent::Quality);
            return sdk::Status::Ok;
        }
        sdk::Status HandleEvent(const sdk::InputEvent& event,
                                sdk::ApplicationContext& context) override {
            Apply(controller_.Handle(event), context);
            return sdk::Status::Ok;
        }
        sdk::Status HandleIdle(sdk::ApplicationContext& context) override {
            Apply(controller_.Tick(owner_->time_->MonotonicMicroseconds()), context);
            return sdk::Status::Ok;
        }
        sdk::Status Render(const sdk::RenderRequest& request) override {
            using zectrix::app::GalleryPage;
            const bool quality = request.intent == sdk::RenderIntent::Quality;
            if (controller_.page() == GalleryPage::Menu) {
                static constexpr const char* kItems[] = {
                    "LIGHTHOUSE / 1BPP", "FOOTPRINTS / PARTIAL",
                    "MOUNTAIN / 16 GRAY", "RUN ALL SCENES"};
                return ToSdkStatus(owner_->ui_.ShowMenu("DISPLAY GALLERY", kItems,
                    std::size(kItems), controller_.selected(),
                    "UP/DOWN Move  OK View  Hold OK Home", quality));
            }
            const uint32_t image = controller_.image();
            if (controller_.page() == GalleryPage::Report) {
                static constexpr const char* kTitles[] = {"LIGHTHOUSE", "FOOTPRINTS", "MOUNTAIN LANDSCAPE"};
                static constexpr const char* kModes[] = {"FULL", "PARTIAL", "FULL + PRE-CLEAR"};
                return ToSdkStatus(owner_->ui_.ShowSceneInfo(kTitles[image], kModes[image],
                    image == 2 ? "4BPP / 16 GRAY" : "1BPP B/W",
                    image == 2 ? zectrix::display::DisplayService::kFrameBytes4Bpp
                               : zectrix::display::DisplayService::kFrameBytes1Bpp,
                    result_.elapsed_ms, result_.error, quality));
            }

            const int64_t started = owner_->time_->MonotonicMicroseconds();
            if (controller_.frame() == 0) {
                result_ = {};
                remaining_steps_ = 0;
                if (image == 0) {
                    result_.error = owner_->ui_.ShowImage1Bpp(kLighthouse1bppStart,
                        static_cast<size_t>(kLighthouse1bppEnd - kLighthouse1bppStart));
                } else if (image == 1) {
                    result_.error = StartFootprints();
                } else {
                    result_.error = owner_->ui_.ShowImage4Bpp(kMountain4bppStart,
                        static_cast<size_t>(kMountain4bppEnd - kMountain4bppStart));
                }
            } else {
                result_.error = DrawFootprint();
            }
            const int64_t completed = owner_->time_->MonotonicMicroseconds();
            result_.elapsed_ms += (completed - started) / 1000;
            const bool more = image == 1 && remaining_steps_ > 0;
            const int64_t hold = image == 2 ? 5000000 : image == 0 ? 2500000 :
                more ? (controller_.frame() == 0 ? 900000 : 400000) : 2200000;
            controller_.Presented(completed, result_.error == ESP_OK, more, hold);
            return ToSdkStatus(result_.error);
        }
        sdk::Status Exit() override {
            owner_->gallery_selection_ = controller_.selected();
            controller_.Stop();
            return sdk::Status::Ok;
        }

    private:
        static void Apply(zectrix::app::GalleryDecision decision, sdk::ApplicationContext& context) {
            using Decision = zectrix::app::GalleryDecision;
            if (decision == Decision::Shutdown) context.RequestCommand(sdk::AppCommand::Shutdown());
            else if (decision == Decision::Home) context.RequestCommand(sdk::AppCommand::Home());
            else if (decision == Decision::RenderFast || decision == Decision::RenderQuality) {
                context.RequestRender({0, 24, 400, 276}, decision == Decision::RenderQuality
                    ? sdk::RenderIntent::Quality : sdk::RenderIntent::Fast);
            }
        }
        esp_err_t StartFootprints() {
            const size_t size = static_cast<size_t>(kFootprintAnimationEnd - kFootprintAnimationStart);
            if (size < kAnimationHeaderSize ||
                std::memcmp(kFootprintAnimationStart, kFootprintMagic, sizeof(kFootprintMagic)) != 0)
                return ESP_ERR_INVALID_SIZE;
            remaining_steps_ = ReadLe16(kFootprintAnimationStart + 4);
            animation_cursor_ = kFootprintAnimationStart + kAnimationHeaderSize;
            return owner_->ui_.ShowImage1Bpp(kSnowPath1bppStart,
                static_cast<size_t>(kSnowPath1bppEnd - kSnowPath1bppStart));
        }
        esp_err_t DrawFootprint() {
            if (!remaining_steps_ || !animation_cursor_ ||
                static_cast<size_t>(kFootprintAnimationEnd - animation_cursor_) < kStepHeaderSize)
                return ESP_ERR_INVALID_SIZE;
            const zectrix::display::Rect region = {ReadLe16(animation_cursor_),
                ReadLe16(animation_cursor_ + 2), ReadLe16(animation_cursor_ + 4),
                ReadLe16(animation_cursor_ + 6)};
            const size_t size = ReadLe16(animation_cursor_ + 10);
            animation_cursor_ += kStepHeaderSize;
            if (size > static_cast<size_t>(kFootprintAnimationEnd - animation_cursor_))
                return ESP_ERR_INVALID_SIZE;
            const esp_err_t result = owner_->ui_.ShowImagePatch(region, animation_cursor_, size);
            animation_cursor_ += size;
            --remaining_steps_;
            return result;
        }

        TerminalApp* owner_;
        zectrix::app::GalleryController controller_;
        SceneResult result_{};
        const uint8_t* animation_cursor_ = nullptr;
        uint16_t remaining_steps_ = 0;
    };

    class ShowcaseApplication final : public GalleryApplication {
    public:
        explicit ShowcaseApplication(TerminalApp& owner) : GalleryApplication(owner, true) {}
    };

    class DeviceInfoApplication : public sdk::Application {
    public:
        explicit DeviceInfoApplication(TerminalApp& owner, bool about = false)
            : owner_(&owner), about_(about) {}
        sdk::Status Enter(sdk::ApplicationContext& context) override {
            if (!about_) {
                const esp_err_t read = owner_->system_->ReadSnapshot(&system_);
                if (read != ESP_OK) return ToSdkStatus(read);
                power_ = owner_->power_snapshot_;
            }
            context.RequestRender({0, 24, 400, 276}, sdk::RenderIntent::Quality);
            return sdk::Status::Ok;
        }
        sdk::Status HandleEvent(const sdk::InputEvent& event,
                                sdk::ApplicationContext& context) override {
            const auto decision = zectrix::app::HandleClockInput(event);
            if (decision == zectrix::app::ClockDecision::Home) context.RequestCommand(sdk::AppCommand::Home());
            if (decision == zectrix::app::ClockDecision::Shutdown) context.RequestCommand(sdk::AppCommand::Shutdown());
            return sdk::Status::Ok;
        }
        sdk::Status HandleIdle(sdk::ApplicationContext& context) override {
            const auto& current = owner_->power_snapshot_;
            if (!about_ && (current.battery_valid != power_.battery_valid ||
                current.battery_percent != power_.battery_percent || current.battery_mv != power_.battery_mv ||
                current.external_power_present != power_.external_power_present || current.charging != power_.charging)) {
                power_ = current;
                context.RequestRender({0, 24, 400, 276}, sdk::RenderIntent::Fast);
            }
            return sdk::Status::Ok;
        }
        sdk::Status Render(const sdk::RenderRequest& request) override {
            const bool quality = request.intent == sdk::RenderIntent::Quality;
            return ToSdkStatus(about_ ? owner_->ui_.ShowAbout(quality)
                : owner_->ui_.ShowDeviceInfo(power_, system_, quality));
        }
        sdk::Status Exit() override { return sdk::Status::Ok; }

    private:
        TerminalApp* owner_;
        bool about_;
        zectrix::power::PowerSnapshot power_{};
        zectrix::system::SystemSnapshot system_{};
    };

    class AboutApplication final : public DeviceInfoApplication {
    public:
        explicit AboutApplication(TerminalApp& owner) : DeviceInfoApplication(owner, true) {}
    };

    void UpdateSystemStatus() {
        const int64_t now = time_->MonotonicMicroseconds();
        if (now >= next_power_sample_us_) {
            power_snapshot_ = power_->ReadSnapshot();
            connectivity_->UpdatePower(power_snapshot_);
            status_.battery_valid = power_snapshot_.battery_valid && !power_snapshot_.battery_absent;
            status_.battery_percent = std::min<uint8_t>(power_snapshot_.battery_percent, 100);
            status_.charging = power_snapshot_.charging;
            status_.external_power = power_snapshot_.external_power_present;
            status_.charge_fault = power_snapshot_.charge_fault;
            next_power_sample_us_ = now + 5000000;
        }
        if (now >= next_clock_sample_us_) {
            zectrix::time::DateTime value;
            status_.time_valid = time_->ReadRtc(&value) == ESP_OK;
            if (!status_.time_valid) {
                const auto fallback = time_->Now();
                value = fallback.value;
                status_.time_valid = fallback.source == zectrix::time::ClockSource::System;
            }
            status_.hour = status_.time_valid ? static_cast<uint8_t>(value.hour) : 0;
            status_.minute = status_.time_valid ? static_cast<uint8_t>(value.minute) : 0;
            next_clock_sample_us_ = now + 1000000;
        }
        const auto link = connectivity_->Snapshot();
        using Indicator = zectrix::ui::RadioIndicator;
        using Ble = zectrix::connectivity::ConnectivityState;
        switch (link.state) {
            case Ble::kStopped: status_.ble = Indicator::Off; break;
            case Ble::kIdle:
            case Ble::kAdvertising: status_.ble = Indicator::Ready; break;
            case Ble::kPairing:
            case Ble::kSecuring: status_.ble = Indicator::Busy; break;
            case Ble::kSecure:
            case Ble::kLinkReady:
            case Ble::kProtocolNegotiatedLocal: status_.ble = Indicator::Connected; break;
            case Ble::kFault: status_.ble = Indicator::Fault; break;
        }
        using Wifi = zectrix::connectivity::WifiBackendState;
        switch (link.wifi_state) {
            case Wifi::kStopped:
            case Wifi::kLoadingCredentials: status_.wifi = Indicator::Off; break;
            case Wifi::kResolving:
            case Wifi::kOpeningTls:
            case Wifi::kTransferring: status_.wifi = Indicator::Connected; break;
            case Wifi::kStopFailed: status_.wifi = Indicator::Fault; break;
            default: status_.wifi = Indicator::Busy; break;
        }
        ui_.UpdateStatus(status_);
    }

    void RunApplicationShell() {
        OwnedFactory<LauncherApplication> launcher_factory(*this);
        OwnedFactory<ClockApplication> clock_factory(*this);
        OwnedFactory<SettingsApplication> settings_factory(*this);
        OwnedFactory<ConnectivityApplication> connectivity_factory(*this);
        OwnedFactory<DiagnosticsApplication> diagnostics_factory(*this);
        OwnedFactory<GalleryApplication> gallery_factory(*this);
        OwnedFactory<ShowcaseApplication> showcase_factory(*this);
        OwnedFactory<DeviceInfoApplication> device_info_factory(*this);
        OwnedFactory<AboutApplication> about_factory(*this);
        const sdk::ApplicationDescriptor descriptors[] = {
            {"launcher", "Launcher", &launcher_factory},
            {"clock", "Clock", &clock_factory},
            {"settings", "Settings", &settings_factory},
            {"connectivity", "Connectivity", &connectivity_factory},
            {"diagnostics", "Diagnostics", &diagnostics_factory},
            {"gallery", "Display Gallery", &gallery_factory},
            {"showcase", "Auto Showcase", &showcase_factory},
            {"device-info", "Device Info", &device_info_factory},
            {"about", "About", &about_factory},
        };
        sdk::ApplicationRuntime runtime(descriptors, std::size(descriptors), "launcher", *this);
        if (!sdk::IsOk(runtime.Start())) return;
        // Retain the boot-evidence marker consumed by the maintenance smoke test.
        LogHeap("M3 runtime active");
        if (!sdk::IsOk(runtime.Step())) return;
        // Confirm a trial image only after platform startup and the first
        // launcher frame have both succeeded.
        const auto confirmed = platform_.Update().ConfirmBoot();
        if (confirmed != zectrix::update::Result::kOk) {
            ESP_LOGE(kTag, "boot confirmation failed: %s", zectrix::update::ResultName(confirmed));
            return;
        }
        while (runtime.state() == sdk::LifecycleState::Active) {
            sdk::InputEvent event;
            const bool received = input_->Wait(&event, kOwnerPollTimeout);
            UpdateSystemStatus();
            const sdk::Status result = received ? runtime.Step(&event) : runtime.Idle();
            if (!sdk::IsOk(result)) {
                ESP_LOGE(kTag, "application step failed: %s", sdk::StatusName(result));
            } else if (runtime.state() == sdk::LifecycleState::Active) {
                const esp_err_t status = ui_.RefreshPending();
                if (status != ESP_OK) ESP_LOGW(kTag, "status refresh failed: %s", esp_err_to_name(status));
            }
        }
    }

    sdk::Status Shutdown() override {
        PowerOff();
    }

    void EnterFailsafe(sdk::Status reason) override {
        ESP_LOGE(kTag, "application runtime failsafe: %s",
                 sdk::StatusName(reason));
    }

    void LogHeap(const char* phase) {
        zectrix::system::SystemSnapshot snapshot;
        const esp_err_t read = system_->ReadSnapshot(&snapshot);
        if (read != ESP_OK) {
            ESP_LOGW(kTag, "heap snapshot failed: %s", esp_err_to_name(read));
            return;
        }
        ESP_LOGI(kTag, "heap %s: free=%lu min=%lu largest=%lu", phase,
                 static_cast<unsigned long>(
                     snapshot.diagnostics.free_internal_heap_bytes),
                 static_cast<unsigned long>(
                     snapshot.diagnostics.minimum_free_internal_heap_bytes),
                 static_cast<unsigned long>(
                     snapshot.diagnostics.largest_internal_heap_block_bytes));
    }

    ControlResult Wait(TickType_t duration, bool any_click_returns) {
        const TickType_t start = xTaskGetTickCount();
        while (xTaskGetTickCount() - start < duration) {
            UpdateSystemStatus();
            const esp_err_t draw = ui_.RefreshPending();
            if (draw != ESP_OK) ESP_LOGW(kTag, "status refresh failed: %s", esp_err_to_name(draw));
            sdk::InputEvent event;
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

    [[noreturn]] void PowerOff() {
        platform_.StopMaintenance();
        const auto stopped = connectivity_->Stop();
        if (stopped != zectrix::connectivity::ConnectivityResult::kOk) {
            ESP_LOGW(kTag, "connectivity stop incomplete before shutdown");
        }
        ESP_LOGI(kTag, "clearing display before shutdown");
        const esp_err_t clear = ui_.ClearDisplay();
        if (clear != ESP_OK) {
            ESP_LOGW(kTag, "display clear failed: %s", esp_err_to_name(clear));
        }
        ESP_LOGI(kTag, "releasing platform peripherals before shutdown");
        platform_.Shutdown();
    }

    zectrix::Platform platform_;
    zectrix::input::InputService* input_ = nullptr;
    zectrix::power::PowerService* power_ = nullptr;
    zectrix::display::DisplayService* display_ = nullptr;
    zectrix::time::TimeService* time_ = nullptr;
    zectrix::storage::StorageService* storage_ = nullptr;
    zectrix::system::SystemService* system_ = nullptr;
    zectrix::connectivity::ConnectivityService* connectivity_ = nullptr;
    ZectrixDemoUi ui_;
    ZectrixSelfTest* tests_ = nullptr;
    std::array<ZectrixTestState,
               static_cast<size_t>(ZectrixTestId::kCount)> test_states_;
    size_t launcher_selection_ = 0;
    uint32_t gallery_selection_ = 0;
    zectrix::power::PowerSnapshot power_snapshot_{};
    zectrix::ui::StatusBarState status_{};
    int64_t next_power_sample_us_ = 0;
    int64_t next_clock_sample_us_ = 0;
};

}  // namespace

extern "C" void app_main(void) {
    static TerminalApp app;
    app.Run();
}
