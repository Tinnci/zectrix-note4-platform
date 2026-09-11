#include "zectrix_wifi_backend.h"

#include <algorithm>
#include <cstring>

namespace zectrix::connectivity {
namespace {

WifiOperationResult MapDriverFailure(WifiDriverResult result) {
    switch (result) {
        case WifiDriverResult::kUnavailable:
            return WifiOperationResult::kUnavailable;
        case WifiDriverResult::kAuthRejected:
            return WifiOperationResult::kAuthRejected;
        case WifiDriverResult::kIpFailure:
            return WifiOperationResult::kIpFailure;
        case WifiDriverResult::kDnsFailure:
            return WifiOperationResult::kDnsFailure;
        case WifiDriverResult::kTlsFailure:
            return WifiOperationResult::kTlsFailure;
        case WifiDriverResult::kTransferFailure:
            return WifiOperationResult::kTransferFailure;
        case WifiDriverResult::kServerError:
            return WifiOperationResult::kServerError;
        case WifiDriverResult::kResponseTooLarge:
            return WifiOperationResult::kResponseTooLarge;
        case WifiDriverResult::kInvalidResponse:
            return WifiOperationResult::kInvalidResponse;
        case WifiDriverResult::kReady:
        case WifiDriverResult::kPending:
            return WifiOperationResult::kNone;
    }
    return WifiOperationResult::kUnavailable;
}

}  // namespace

bool ValidateWifiCredentials(const WifiCredentials& credentials) {
    const auto* ssid_end = static_cast<const char*>(std::memchr(
        credentials.ssid.data(), '\0', credentials.ssid.size()));
    const auto* password_end = static_cast<const char*>(std::memchr(
        credentials.passphrase.data(), '\0', credentials.passphrase.size()));
    if (ssid_end == nullptr || ssid_end == credentials.ssid.data() ||
        password_end == nullptr) return false;
    const std::size_t size = password_end - credentials.passphrase.data();
    if (size == 0) return true;
    if (size < 8) return false;
    for (std::size_t index = 0; index < size; ++index) {
        const unsigned char value = credentials.passphrase[index];
        if (size == kMaximumWifiPassphraseBytes) {
            if (!((value >= '0' && value <= '9') ||
                  (value >= 'a' && value <= 'f') ||
                  (value >= 'A' && value <= 'F'))) return false;
        } else if (value < 0x20 || value > 0x7e) {
            return false;
        }
    }
    return true;
}

WifiBackend::WifiBackend(WifiCredentialSource* credentials,
                         WifiBackendDriver* driver)
    : credentials_(credentials), driver_(driver) {}

WifiBackend::~WifiBackend() {
    ClearWifiCredentials(&loaded_credentials_);
    if (station_started_ && driver_ != nullptr) {
        driver_->StopStation();
    }
}

bool WifiBackend::Begin(const WifiBackendRequest& request, uint32_t now_ms) {
    if (credentials_ == nullptr || driver_ == nullptr ||
        state_ != WifiBackendState::kStopped ||
        outcome_ready_ || request.maximum_response_bytes == 0 ||
        request.maximum_response_bytes > kMaximumWifiResourceBytes ||
        request.timeout_ms < kMinimumTimeoutMs ||
        request.timeout_ms > kMaximumTimeoutMs) {
        return false;
    }

    request_ = request;
    outcome_ = {};
    current_ms_ = now_ms;
    if (request.capability !=
        companion::ResourceCapability::kPublicTestDocumentV1) {
        outcome_.operation = WifiOperationResult::kUnsupportedCapability;
        outcome_.stop = WifiStopResult::kNotRequired;
        outcome_ready_ = true;
        return true;
    }
    deadline_ms_ = now_ms + request.timeout_ms;
    station_started_ = false;
    state_ = WifiBackendState::kLoadingCredentials;
    return true;
}

void WifiBackend::Poll(uint32_t now_ms) {
    if (!Busy()) return;
    current_ms_ = now_ms;
    if (state_ != WifiBackendState::kStopping &&
        DeadlineReached(now_ms, deadline_ms_)) {
        Fail(WifiOperationResult::kTimeout);
    }

    switch (state_) {
        case WifiBackendState::kStopped:
        case WifiBackendState::kStopFailed:
            return;
        case WifiBackendState::kLoadingCredentials: {
            const WifiCredentialResult result =
                credentials_->Load(&loaded_credentials_);
            if (result == WifiCredentialResult::kUnavailable) {
                Fail(WifiOperationResult::kCredentialsUnavailable);
            } else if (result != WifiCredentialResult::kAvailable ||
                       !ValidateWifiCredentials(loaded_credentials_)) {
                Fail(WifiOperationResult::kInvalidCredentials);
            } else {
                state_ = WifiBackendState::kStartingStation;
            }
            break;
        }
        case WifiBackendState::kStartingStation: {
            const WifiDriverResult result =
                driver_->StartStation(loaded_credentials_);
            if (result == WifiDriverResult::kReady ||
                result == WifiDriverResult::kPending) {
                station_started_ = true;
                state_ = WifiBackendState::kAssociating;
            } else {
                Fail(MapDriverFailure(result));
            }
            ClearWifiCredentials(&loaded_credentials_);
            break;
        }
        case WifiBackendState::kAssociating:
            ProcessDriverResult(driver_->PollAssociation(),
                                WifiBackendState::kWaitingForIp);
            break;
        case WifiBackendState::kWaitingForIp:
            ProcessDriverResult(driver_->PollIp(),
                                WifiBackendState::kResolving);
            break;
        case WifiBackendState::kResolving:
            ProcessDriverResult(driver_->Resolve(request_.capability),
                                WifiBackendState::kOpeningTls);
            break;
        case WifiBackendState::kOpeningTls:
            ProcessDriverResult(driver_->OpenTls(request_.capability),
                                WifiBackendState::kTransferring);
            break;
        case WifiBackendState::kTransferring: {
            std::size_t body_size = 0;
            const WifiDriverResult result = driver_->Fetch(
                request_.capability, outcome_.body.data(),
                request_.maximum_response_bytes, &body_size);
            if (result == WifiDriverResult::kReady) {
                if (body_size == 0 ||
                    body_size > request_.maximum_response_bytes) {
                    Fail(body_size > request_.maximum_response_bytes
                             ? WifiOperationResult::kResponseTooLarge
                             : WifiOperationResult::kInvalidResponse);
                } else {
                    outcome_.body_size = body_size;
                    outcome_.operation = WifiOperationResult::kSuccess;
                    BeginStopping();
                }
            } else if (result != WifiDriverResult::kPending) {
                Fail(MapDriverFailure(result));
            }
            break;
        }
        case WifiBackendState::kStopping:
            StopNow(now_ms);
            break;
    }
}

void WifiBackend::Cancel() {
    if (!Busy() || state_ == WifiBackendState::kStopping) return;
    Fail(WifiOperationResult::kCancelled);
}

bool WifiBackend::TakeOutcome(WifiBackendOutcome* outcome) {
    if (outcome == nullptr || !outcome_ready_) return false;
    *outcome = outcome_;
    outcome_ = {};
    outcome_ready_ = false;
    return true;
}

void ClearWifiCredentials(WifiCredentials* credentials) {
    if (credentials == nullptr) return;
    volatile char* ssid = credentials->ssid.data();
    for (std::size_t i = 0; i < credentials->ssid.size(); ++i) ssid[i] = 0;
    volatile char* passphrase = credentials->passphrase.data();
    for (std::size_t i = 0; i < credentials->passphrase.size(); ++i) {
        passphrase[i] = 0;
    }
}

bool WifiBackend::DeadlineReached(uint32_t now_ms, uint32_t deadline_ms) {
    return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

void WifiBackend::Fail(WifiOperationResult result) {
    if (outcome_.operation == WifiOperationResult::kNone ||
        outcome_.operation == WifiOperationResult::kSuccess) {
        outcome_.operation = result;
        outcome_.body_size = 0;
    }
    ClearWifiCredentials(&loaded_credentials_);
    if (station_started_) {
        BeginStopping();
    } else {
        outcome_.stop = WifiStopResult::kNotRequired;
        state_ = WifiBackendState::kStopped;
        outcome_ready_ = true;
    }
}

void WifiBackend::ProcessDriverResult(WifiDriverResult result,
                                      WifiBackendState success_state) {
    if (result == WifiDriverResult::kReady) {
        state_ = success_state;
    } else if (result != WifiDriverResult::kPending) {
        Fail(MapDriverFailure(result));
    }
}

void WifiBackend::BeginStopping() {
    if (state_ == WifiBackendState::kStopping) return;
    stop_deadline_ms_ = current_ms_ + kStopTimeoutMs;
    state_ = WifiBackendState::kStopping;
}

void WifiBackend::StopNow(uint32_t now_ms) {
    const WifiDriverResult result = driver_->StopStation();
    // ESP teardown is idempotent and retains ownership on errors. Give transient
    // stop/deinit failures the existing cleanup window before latching failure.
    if (result != WifiDriverResult::kReady &&
        !DeadlineReached(now_ms, stop_deadline_ms_)) {
        return;
    }
    outcome_.stop = result == WifiDriverResult::kReady
                        ? WifiStopResult::kSuccess
                        : WifiStopResult::kFailure;
    if (result == WifiDriverResult::kReady) {
        station_started_ = false;
        state_ = WifiBackendState::kStopped;
    } else {
        state_ = WifiBackendState::kStopFailed;
    }
    outcome_ready_ = true;
}

}  // namespace zectrix::connectivity
