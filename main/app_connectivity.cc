#include "terminal_internal.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "zectrix_first_party_app_controllers.h"

#include "zectrix_connectivity_service.h"
#if CONFIG_ZECTRIX_ENABLE_READER
#include "zectrix_reader_platform.h"
#endif

namespace zectrix::terminal {

class TerminalApp::ConnectivityApplication final : public sdk::Application {
public:
    explicit ConnectivityApplication(TerminalApp& owner) : owner_(&owner) {}

    sdk::Status Enter(sdk::ApplicationContext& context) override {
        status_ = "SELECT AN ACTION WITH UP / DOWN";
        passkey_[0] = '\0';
        displayed_state_ = owner_->connectivity_->State();
        const auto started = controller_.Start();
        return sdk::IsOk(started) ? Apply(controller_.Tick(), context) : started;
    }

    sdk::Status HandleEvent(const sdk::InputEvent& event,
                            sdk::ApplicationContext& context) override {
        return Apply(controller_.Handle(event), context);
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
        return Apply(controller_.Tick(), context);
    }

    sdk::Status Render(const sdk::RenderRequest& request) override {
        const bool quality = request.intent == sdk::RenderIntent::Quality;
        esp_err_t result;
        if (controller_.page() == app::ConnectivityPage::Forget) {
            static constexpr const char* kItems[] = {"KEEP TRUSTED PHONE", "FORGET PHONE AND SYNC LINK"};
            result = owner_->ui_.ShowMenu("FORGET PHONE", kItems, std::size(kItems),
                controller_.selected(), "UP/DOWN Move  OK Confirm  Hold OK Cancel", quality);
        } else {
            result = owner_->ui_.ShowConnectivity(StateText(owner_->connectivity_->State()), status_,
                passkey_[0] == '\0' ? nullptr : passkey_, controller_.selected(), quality);
        }
        controller_.Presented(result == ESP_OK);
        return ToSdkStatus(result);
    }

    sdk::Status Exit() override {
        controller_.Stop();
        std::memset(passkey_, 0, sizeof(passkey_));
        return sdk::Status::Ok;
    }

private:
    sdk::Status Apply(app::ConnectivityDecision decision, sdk::ApplicationContext& context) {
        using Decision = app::ConnectivityDecision;
        if (decision == Decision::Back) return owner_->RequestBack(context);
        if (decision == Decision::Shutdown) {
            context.RequestCommand(sdk::AppCommand::Shutdown());
            return sdk::Status::Ok;
        }
        if (decision == Decision::None) return sdk::Status::Ok;
        if (decision == Decision::StartPairing) {
            const auto result = owner_->connectivity_->StartLocalPairing();
            status_ = result == zectrix::connectivity::ConnectivityResult::kOk
                          ? "PHONE CAN PAIR FOR 120 SECONDS"
                          : "PAIRING IS NOT AVAILABLE";
        } else if (decision == Decision::FetchResource) {
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
        } else if (decision == Decision::ClearBonds) {
            const auto result = owner_->connectivity_->ClearPeerBonds();
            status_ = result == zectrix::connectivity::ConnectivityResult::kOk
                          ? "TRUSTED PHONE FORGOTTEN"
                          : "DISCONNECT BEFORE FORGETTING";
#if CONFIG_ZECTRIX_ENABLE_READER
            if (result == zectrix::connectivity::ConnectivityResult::kOk) {
                zectrix::reader::PlatformBookmarkStore store(*owner_->storage_, *owner_->connectivity_);
                zectrix::reader::Bookmarks bookmarks(store);
                if (bookmarks.Load() != zectrix::reader::Result::Ok ||
                    bookmarks.ResetPeer() != zectrix::reader::Result::Ok) {
                    status_ = "PHONE RESET; READER SYNC ERROR";
                    ESP_LOGW(kTag, "reader phone cursor reset failed");
                }
            }
#endif
        }
        return context.RequestRender({0, 24, 400, 276},
            decision == Decision::RenderQuality || decision == Decision::ClearBonds
                ? sdk::RenderIntent::Quality : sdk::RenderIntent::Fast)
            ? sdk::Status::Ok : sdk::Status::InternalError;
    }

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
    app::ConnectivityController controller_;
    const char* status_ = "";
    char resource_status_[48]{};
    char passkey_[7]{};
    zectrix::connectivity::ConnectivityState displayed_state_ =
        zectrix::connectivity::ConnectivityState::kStopped;
};

sdk::Status TerminalApp::CreateConnectivity(TerminalApp& owner, sdk::Application** output) {
    return CreateApplication<ConnectivityApplication>(owner, output);
}

}  // namespace zectrix::terminal
