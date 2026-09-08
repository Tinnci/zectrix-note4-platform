#include "zectrix_resource_client.h"

#include <algorithm>
#include <cstring>

namespace zectrix::connectivity {
namespace {

constexpr uint32_t kDefaultRetryMs = 10000;
constexpr uint32_t kMaximumRetryMs = 300000;

uint32_t RetryDelay(uint32_t requested) {
    return std::clamp<uint32_t>(requested == 0 ? kDefaultRetryMs : requested,
                               1000, kMaximumRetryMs);
}

bool Reached(uint32_t now, uint32_t deadline) {
    return static_cast<int32_t>(now - deadline) >= 0;
}

uint32_t Remaining(bool active, uint32_t deadline, uint32_t now) {
    return !active || Reached(now, deadline) ? 0 : deadline - now;
}

companion::ResourceStatus WifiStatus(WifiOperationResult result) {
    using Status = companion::ResourceStatus;
    switch (result) {
        case WifiOperationResult::kSuccess: return Status::kSuccess;
        case WifiOperationResult::kTimeout: return Status::kTimeout;
        case WifiOperationResult::kResponseTooLarge: return Status::kResponseTooLarge;
        case WifiOperationResult::kServerError: return Status::kServerError;
        case WifiOperationResult::kUnsupportedCapability: return Status::kUnsupportedCapability;
        case WifiOperationResult::kAuthRejected:
        case WifiOperationResult::kInvalidCredentials: return Status::kNotAuthorized;
        case WifiOperationResult::kTlsFailure:
        case WifiOperationResult::kInvalidResponse: return Status::kInvalidResponse;
        default: return Status::kPhoneUnavailable;
    }
}

}  // namespace

bool ResourceClient::Begin(uint32_t request_id,
                           const companion::ResourceRequestMessage& request,
                           uint32_t now_ms) {
    if (Busy() || response_ready_ || request_id == 0 ||
        request.maximum_response_bytes == 0 ||
        request.maximum_response_bytes > companion::kResourceMaximumBodySize ||
        request.timeout_ms < companion::kResourceMinimumTimeoutMs ||
        request.timeout_ms > companion::kResourceMaximumTimeoutMs) return false;
    request_ = request;
    response_ = {};
    response_.request_id = request_id;
    decision_ = {};
    cancelled_ = false;
    retry_waiting_ = false;
    deadline_ms_ = now_ms + request.timeout_ms;
    state_ = State::kQueued;
    if (request.capability != companion::ResourceCapability::kPublicTestDocumentV1) {
        SetFailure(companion::ResourceStatus::kUnsupportedCapability);
        Finish();
    }
    return true;
}

companion::ConnectivityConditions ResourceClient::Conditions(
    companion::ConnectivityConditions conditions, uint32_t now_ms) const {
    conditions.phone_retry_after_ms = std::max(conditions.phone_retry_after_ms,
        Remaining(phone_backoff_, phone_retry_at_ms_, now_ms));
    conditions.wifi_retry_after_ms = std::max(conditions.wifi_retry_after_ms,
        Remaining(wifi_backoff_, wifi_retry_at_ms_, now_ms));
    if (wifi_.State() == WifiBackendState::kStopFailed) {
        conditions.wifi_credentials_available = false;
    }
    return conditions;
}

void ResourceClient::Poll(const companion::ConnectivityConditions& current,
                          uint32_t now_ms) {
    if (!Busy()) return;
    const auto conditions = Conditions(current, now_ms);
    const bool forbidden = conditions.power_state == companion::ProductPowerState::kShutdown ||
        conditions.user_policy == companion::UserConnectivityPolicy::kOffline;
    if (forbidden && !cancelled_) Cancel(now_ms);
    if (state_ == State::kWifi) {
        if ((!conditions.external_power && conditions.battery_percent <
                companion::ConnectivityPolicy::kMinimumWifiBatteryPercent) ||
            conditions.user_policy == companion::UserConnectivityPolicy::kPhoneOnly) {
            wifi_.Cancel();
        }
        wifi_.Poll(now_ms);
        if (!wifi_.TakeOutcome(&wifi_outcome_)) return;
        response_.path = companion::ConnectivityPath::kDirectWifi;
        response_.wifi_operation = wifi_outcome_.operation;
        response_.wifi_stop = wifi_outcome_.stop;
        SetFailure(WifiStatus(wifi_outcome_.operation));
        if (wifi_outcome_.operation == WifiOperationResult::kSuccess) {
            response_.content_type = companion::ResourceContentType::kTextPlainUtf8;
            response_.body_size = wifi_outcome_.body_size;
            std::copy_n(wifi_outcome_.body.begin(), response_.body_size, response_.body.begin());
        }
        CompleteAttempt(now_ms);
    }
    if (!Busy()) return;
    if (state_ == State::kPhone) {
        if (conditions.phone != companion::PhoneAvailability::kConnected ||
            conditions.user_policy == companion::UserConnectivityPolicy::kWifiOnly) {
            PhoneDisconnected(now_ms);
        } else if (Reached(now_ms, deadline_ms_)) {
            SetFailure(companion::ResourceStatus::kTimeout);
            CompleteAttempt(now_ms);
        } else {
            return;
        }
    }
    if (state_ != State::kQueued && state_ != State::kRetry) return;

    companion::ResourceRequest semantic{};
    semantic.capability = request_.capability;
    semantic.expected_response_size = request_.maximum_response_bytes;
    semantic.deadline_ms = request_.timeout_ms;
    semantic.durable = request_.durable;
    decision_ = companion::ConnectivityPolicy::Decide(semantic, Conditions(current, now_ms));
    if (decision_.path == companion::ConnectivityPath::kDefer) {
        if (retry_waiting_ && !Reached(now_ms, retry_at_ms_)) return;
        if (state_ != State::kRetry && !retry_waiting_) {
            SetFailure(companion::ResourceStatus::kPhoneUnavailable);
        }
        if (request_.durable && !forbidden && !cancelled_) {
            response_.retry_after_ms = RetryDelay(decision_.retry_after_ms);
            response_.retry_queued = true;
            response_ready_ = true;
            retry_at_ms_ = now_ms + response_.retry_after_ms;
            retry_waiting_ = true;
            state_ = State::kQueued;
        } else {
            Finish();
        }
        return;
    }
    // Progress is advisory. An unread retry notice must not block background
    // work; replace it when a path becomes available, even before the timer.
    response_ready_ = false;
    retry_waiting_ = false;
    SetFailure(companion::ResourceStatus::kInvalidResponse);
    response_.path = decision_.path;
    deadline_ms_ = now_ms + request_.timeout_ms;
    if (decision_.path == companion::ConnectivityPath::kDirectWifi) {
        const WifiBackendRequest request{
            request_.capability, request_.maximum_response_bytes, request_.timeout_ms};
        if (wifi_.Begin(request, now_ms)) {
            state_ = State::kWifi;
        } else {
            SetFailure(companion::ResourceStatus::kPhoneUnavailable);
            CompleteAttempt(now_ms);
        }
    } else {
        const auto sent = phone_.SendResource(response_.request_id, request_);
        if (sent == companion::LinkResult::kOk) {
            state_ = State::kPhone;
        } else {
            SetFailure(companion::ResourceStatus::kPhoneUnavailable);
            CompleteAttempt(now_ms);
        }
    }
}

bool ResourceClient::AcceptPhoneResponse(
    uint32_t request_id, const companion::ResourceResponseMessage& response,
    uint32_t now_ms) {
    if (state_ != State::kPhone || response_.request_id != request_id || cancelled_) return false;
    if (Reached(now_ms, deadline_ms_)) {
        SetFailure(companion::ResourceStatus::kTimeout);
    } else if (response.body_size > request_.maximum_response_bytes) {
        SetFailure(companion::ResourceStatus::kResponseTooLarge);
    } else if ((response.body_size != 0 && response.body == nullptr) ||
        (response.status == companion::ResourceStatus::kSuccess &&
         (response.body_size == 0 || response.content_type !=
             companion::ResourceContentType::kTextPlainUtf8))) {
        SetFailure(companion::ResourceStatus::kInvalidResponse);
    } else {
        SetFailure(response.status);
        response_.content_type = response.content_type;
        response_.body_size = response.body_size;
        response_.retry_after_ms = response.retry_after_ms;
        if (response.body_size != 0) {
            std::memcpy(response_.body.data(), response.body, response.body_size);
        }
    }
    CompleteAttempt(now_ms);
    return true;
}

void ResourceClient::SetFailure(companion::ResourceStatus status) {
    response_.status = status;
    response_.content_type = companion::ResourceContentType::kNone;
    response_.body_size = 0;
    response_.retry_after_ms = 0;
    response_.retry_queued = false;
}

void ResourceClient::CompleteAttempt(uint32_t now_ms) {
    if (!cancelled_ && companion::IsTransientResourceStatus(response_.status)) {
        const uint32_t retry = RetryDelay(response_.retry_after_ms);
        response_.retry_after_ms = retry;
        if (response_.path == companion::ConnectivityPath::kPhoneProxy) {
            phone_retry_at_ms_ = now_ms + retry;
            phone_backoff_ = true;
        } else if (response_.path == companion::ConnectivityPath::kDirectWifi) {
            wifi_retry_at_ms_ = now_ms + retry;
            wifi_backoff_ = true;
        }
        state_ = State::kRetry;
    } else {
        Finish();
    }
}

void ResourceClient::Finish() {
    state_ = State::kIdle;
    response_.retry_queued = false;
    response_ready_ = true;
}

void ResourceClient::PhoneDisconnected(uint32_t now_ms) {
    if (state_ != State::kPhone) return;
    SetFailure(companion::ResourceStatus::kPhoneUnavailable);
    CompleteAttempt(now_ms);
}

void ResourceClient::Cancel(uint32_t) {
    if (!Busy()) return;
    cancelled_ = true;
    response_ready_ = false;
    if (state_ == State::kWifi) {
        wifi_.Cancel();
    } else {
        SetFailure(companion::ResourceStatus::kPhoneUnavailable);
        Finish();
    }
}

bool ResourceClient::TakeResponse(ResourceResponse* response) {
    if (response == nullptr || !response_ready_) return false;
    *response = response_;
    response_ready_ = false;
    return true;
}

uint32_t ResourceClient::NextWakeMs(uint32_t now_ms) const {
    if (!Busy()) return UINT32_MAX;
    if (state_ == State::kWifi) return 20;
    if (state_ == State::kPhone) return Remaining(true, deadline_ms_, now_ms);
    return Remaining(retry_waiting_, retry_at_ms_, now_ms);
}

}  // namespace zectrix::connectivity
