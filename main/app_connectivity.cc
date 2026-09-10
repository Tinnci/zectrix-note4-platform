#include "zectrix_locale.h"
#include "terminal_internal.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "zectrix_first_party_app_controllers.h"

#include "zectrix_connectivity_service.h"
#if CONFIG_ZECTRIX_ENABLE_READER
#include "zectrix_reader_platform.h"
#endif

using zectrix::i18n::Tr;
using zectrix::i18n::Text;

namespace zectrix::terminal {

class TerminalApp::ConnectivityApplication final : public sdk::Application {
public:
    explicit ConnectivityApplication(TerminalApp& owner) : owner_(&owner) {}

    sdk::Status Enter(sdk::ApplicationContext& context) override {
        status_ = Tr(Text::SelectAction);
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
            status_ = Tr(Text::EnterOnPhone);
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
                              ? Tr(Text::HelloAccepted)
                              : current_state ==
                                  zectrix::connectivity::ConnectivityState::kLinkReady
                              ? Tr(Text::VerifyingPhone)
                              : Tr(Text::EnablingUpdates);
            } else if (current_state ==
                       zectrix::connectivity::ConnectivityState::kPairing) {
                status_ = Tr(Text::SelectNote4OnPhone);
            } else if (current_state ==
                       zectrix::connectivity::ConnectivityState::kAdvertising) {
                status_ = Tr(Text::PhoneCanReconnect);
            } else if (current_state ==
                       zectrix::connectivity::ConnectivityState::kFault) {
                status_ = Tr(Text::BluetoothRestart);
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
            const char* kItems[] = {Tr(Text::KeepTrustedPhone), Tr(Text::ForgetPhoneSync)};
            result = owner_->ui_.ShowMenu(Tr(Text::ForgetPhone), kItems, std::size(kItems),
                controller_.selected(), Tr(Text::NavConfirmCancel), quality);
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
                          ? Tr(Text::PairingOpen120)
                          : Tr(Text::PairingUnavailable);
        } else if (decision == Decision::FetchResource) {
            zectrix::companion::ResourceRequestMessage request{};
            owner_->connectivity_->UpdatePower(owner_->power_->ReadSnapshot());
            const auto result =
                owner_->connectivity_->RequestResource(request);
            if (result ==
                zectrix::connectivity::ConnectivityResult::kOk) {
                status_ = Tr(Text::RequestingDocument);
            } else if (result ==
                       zectrix::connectivity::ConnectivityResult::kBusy) {
                status_ = Tr(Text::ResourceBusy);
            } else {
                status_ = Tr(Text::ResourceUnavailable);
            }
        } else if (decision == Decision::ClearBonds) {
            const auto result = owner_->connectivity_->ClearPeerBonds();
            status_ = result == zectrix::connectivity::ConnectivityResult::kOk
                          ? Tr(Text::PhoneForgotten)
                          : Tr(Text::DisconnectBeforeForget);
#if CONFIG_ZECTRIX_ENABLE_READER
            if (result == zectrix::connectivity::ConnectivityResult::kOk) {
                zectrix::reader::PlatformBookmarkStore store(*owner_->storage_, *owner_->connectivity_);
                zectrix::reader::Bookmarks bookmarks(store);
                if (bookmarks.Load() != zectrix::reader::Result::Ok ||
                    bookmarks.ResetPeer() != zectrix::reader::Result::Ok) {
                    status_ = Tr(Text::PhoneResetReaderError);
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
                    Tr(Text::FetchedBytes),
                    static_cast<unsigned>(response.body_size),
                    response.path == zectrix::companion::ConnectivityPath::kDirectWifi
                        ? "WI-FI" : Tr(Text::Phone));
                break;
            case Status::kPhoneUnavailable:
                std::snprintf(resource_status_, sizeof(resource_status_),
                              "%s", response.retry_queued
                                  ? Tr(Text::NetworkRetryQueued)
                                  : Tr(Text::NetworkResourceUnavailable));
                break;
            case Status::kPhoneOffline:
                std::snprintf(resource_status_, sizeof(resource_status_),
                              "%s", response.retry_queued
                                  ? Tr(Text::PhoneOfflineRetry) : Tr(Text::PhoneOffline));
                break;
            case Status::kTimeout:
                std::snprintf(resource_status_, sizeof(resource_status_),
                              "%s", response.retry_queued
                                  ? Tr(Text::RequestTimeoutRetry) : Tr(Text::RequestTimeout));
                break;
            case Status::kServerError:
                std::snprintf(resource_status_, sizeof(resource_status_),
                              "%s", Tr(Text::ResourceServerError));
                break;
            case Status::kResponseTooLarge:
                std::snprintf(resource_status_, sizeof(resource_status_),
                              "%s", Tr(Text::ResourceTooLarge));
                break;
            case Status::kNotAuthorized:
                std::snprintf(resource_status_, sizeof(resource_status_),
                              "%s", response.path ==
                                  zectrix::companion::ConnectivityPath::kDirectWifi
                                  ? Tr(Text::WifiAuthFailed)
                                  : Tr(Text::PhoneAuthRequired));
                break;
            case Status::kUnsupportedCapability:
                std::snprintf(resource_status_, sizeof(resource_status_),
                              "%s", Tr(Text::ResourceUnsupported));
                break;
            default:
                std::snprintf(resource_status_, sizeof(resource_status_),
                              "%s", Tr(Text::ResourceInvalid));
                break;
        }
        if (response.wifi_stop == zectrix::connectivity::WifiStopResult::kFailure) {
            std::snprintf(resource_status_, sizeof(resource_status_),
                          "%s", Tr(Text::WifiStopFailed));
        }
        status_ = resource_status_;
    }

    static const char* StateText(
        zectrix::connectivity::ConnectivityState state) {
        switch (state) {
            case zectrix::connectivity::ConnectivityState::kIdle: return Tr(Text::Offline);
            case zectrix::connectivity::ConnectivityState::kAdvertising:
                return Tr(Text::ReadyToReconnect);
            case zectrix::connectivity::ConnectivityState::kPairing:
                return Tr(Text::PairingOpen);
            case zectrix::connectivity::ConnectivityState::kSecuring:
                return Tr(Text::SecuringLink);
            case zectrix::connectivity::ConnectivityState::kSecure:
                return Tr(Text::SecureLink);
            case zectrix::connectivity::ConnectivityState::kLinkReady:
                return Tr(Text::BleReady);
            case zectrix::connectivity::ConnectivityState::kProtocolNegotiatedLocal:
                return Tr(Text::ProtocolNegotiated);
            case zectrix::connectivity::ConnectivityState::kFault: return Tr(Text::Fault);
            default: return Tr(Text::Stopped);
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
