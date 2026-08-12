#include "zectrix_connectivity_policy.h"

#include "zectrix_companion_protocol.h"

namespace zectrix::companion {
namespace {

ConnectivityDecision Defer(DecisionReason reason, uint32_t retry_after_ms = 0) {
    return {ConnectivityPath::kDefer, reason, retry_after_ms};
}

bool CanUseWifi(const ConnectivityConditions& conditions) {
    return conditions.wifi_credentials_available &&
           (conditions.external_power ||
            conditions.battery_percent >=
                ConnectivityPolicy::kMinimumWifiBatteryPercent) &&
           conditions.wifi_retry_after_ms == 0;
}

}  // namespace

ConnectivityDecision ConnectivityPolicy::Decide(
    const ResourceRequest& request,
    const ConnectivityConditions& conditions) {
    if (request.expected_response_size == 0 ||
        request.expected_response_size > kMaximumPayloadSize) {
        return Defer(DecisionReason::kInvalidRequest);
    }
    if (conditions.power_state == ProductPowerState::kShutdown) {
        return Defer(DecisionReason::kShuttingDown);
    }
    if (conditions.user_policy == UserConnectivityPolicy::kOffline) {
        return Defer(DecisionReason::kUserOffline);
    }

    const bool phone_ready =
        conditions.phone == PhoneAvailability::kConnected &&
        conditions.phone_retry_after_ms == 0;
    const bool wifi_ready = CanUseWifi(conditions);

    if (conditions.user_policy == UserConnectivityPolicy::kPhoneOnly) {
        if (phone_ready) {
            return {ConnectivityPath::kPhoneProxy,
                    DecisionReason::kUserForcedPhone, 0};
        }
        if (conditions.phone_retry_after_ms != 0) {
            return Defer(DecisionReason::kPhoneBackoff,
                         conditions.phone_retry_after_ms);
        }
        return Defer(conditions.phone == PhoneAvailability::kPresent
                         ? DecisionReason::kWaitForPhone
                         : DecisionReason::kPhoneUnavailable);
    }

    if (conditions.user_policy == UserConnectivityPolicy::kWifiOnly) {
        if (wifi_ready) {
            return {ConnectivityPath::kDirectWifi,
                    DecisionReason::kUserForcedWifi, 0};
        }
        if (!conditions.wifi_credentials_available) {
            return Defer(DecisionReason::kMissingWifiCredentials);
        }
        if (!conditions.external_power &&
            conditions.battery_percent < kMinimumWifiBatteryPercent) {
            return Defer(DecisionReason::kInsufficientPower);
        }
        return Defer(DecisionReason::kWifiBackoff,
                     conditions.wifi_retry_after_ms);
    }

    const bool large =
        request.expected_response_size > kPhonePreferredMaximumBytes;
    const bool urgent = request.deadline_ms != 0 &&
                        request.deadline_ms <= kUrgentDeadlineMs;
    if (phone_ready && !large) {
        return {ConnectivityPath::kPhoneProxy,
                DecisionReason::kPhonePreferred, 0};
    }
    if (wifi_ready && (large || urgent ||
                       conditions.phone == PhoneAvailability::kUnavailable ||
                       conditions.phone_retry_after_ms != 0)) {
        DecisionReason reason = DecisionReason::kPhoneUnavailable;
        if (large) reason = DecisionReason::kLargeTransfer;
        else if (urgent) reason = DecisionReason::kUrgentDeadline;
        else if (conditions.phone_retry_after_ms != 0) {
            reason = DecisionReason::kPhoneBackoff;
        }
        return {ConnectivityPath::kDirectWifi, reason, 0};
    }
    if (phone_ready) {
        return {ConnectivityPath::kPhoneProxy,
                DecisionReason::kPhonePreferred, 0};
    }
    if (conditions.phone == PhoneAvailability::kPresent &&
        !urgent && request.durable) {
        return Defer(DecisionReason::kWaitForPhone);
    }
    if (conditions.phone_retry_after_ms != 0 &&
        conditions.wifi_retry_after_ms != 0) {
        const uint32_t retry_after =
            conditions.phone_retry_after_ms < conditions.wifi_retry_after_ms
                ? conditions.phone_retry_after_ms
                : conditions.wifi_retry_after_ms;
        return Defer(DecisionReason::kNoAvailablePath, retry_after);
    }
    if (!conditions.wifi_credentials_available) {
        return Defer(conditions.phone_retry_after_ms != 0
                         ? DecisionReason::kPhoneBackoff
                         : DecisionReason::kMissingWifiCredentials,
                     conditions.phone_retry_after_ms);
    }
    if (!conditions.external_power &&
        conditions.battery_percent < kMinimumWifiBatteryPercent) {
        return Defer(DecisionReason::kInsufficientPower);
    }
    if (conditions.wifi_retry_after_ms != 0) {
        return Defer(DecisionReason::kWifiBackoff,
                     conditions.wifi_retry_after_ms);
    }
    return Defer(DecisionReason::kNoAvailablePath);
}

}  // namespace zectrix::companion
