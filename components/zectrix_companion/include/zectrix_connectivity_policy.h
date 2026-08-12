#pragma once

#include <cstddef>
#include <cstdint>

namespace zectrix::companion {

enum class ResourceCapability : uint16_t {
    kPublicTestDocumentV1 = 1,
};

enum class ConnectivityPath : uint8_t {
    kPhoneProxy = 0,
    kDirectWifi,
    kDefer,
};

enum class DecisionReason : uint8_t {
    kPhonePreferred = 0,
    kLargeTransfer,
    kPhoneUnavailable,
    kUrgentDeadline,
    kUserForcedPhone,
    kUserForcedWifi,
    kUserOffline,
    kWaitForPhone,
    kPhoneBackoff,
    kWifiBackoff,
    kMissingWifiCredentials,
    kInsufficientPower,
    kNoAvailablePath,
    kShuttingDown,
    kInvalidRequest,
};

enum class PhoneAvailability : uint8_t {
    kUnavailable = 0,
    kPresent,
    kConnected,
};

enum class UserConnectivityPolicy : uint8_t {
    kAutomatic = 0,
    kPhoneOnly,
    kWifiOnly,
    kOffline,
};

enum class ProductPowerState : uint8_t {
    kActive = 0,
    kBleConnectedStandby,
    kBleAdvertisingStandby,
    kDeepOfflineStandby,
    kWifiBurst,
    kShutdown,
};

struct ResourceRequest {
    ResourceCapability capability =
        ResourceCapability::kPublicTestDocumentV1;
    std::size_t expected_response_size = 0;
    uint32_t deadline_ms = 0;
    bool durable = true;
};

struct ConnectivityConditions {
    PhoneAvailability phone = PhoneAvailability::kUnavailable;
    UserConnectivityPolicy user_policy = UserConnectivityPolicy::kAutomatic;
    ProductPowerState power_state = ProductPowerState::kActive;
    bool external_power = false;
    uint8_t battery_percent = 100;
    bool wifi_credentials_available = false;
    uint32_t phone_retry_after_ms = 0;
    uint32_t wifi_retry_after_ms = 0;
};

struct ConnectivityDecision {
    ConnectivityPath path = ConnectivityPath::kDefer;
    DecisionReason reason = DecisionReason::kNoAvailablePath;
    uint32_t retry_after_ms = 0;
};

class ConnectivityPolicy {
public:
    static constexpr std::size_t kPhonePreferredMaximumBytes = 2048;
    static constexpr uint32_t kUrgentDeadlineMs = 5000;
    static constexpr uint8_t kMinimumWifiBatteryPercent = 20;

    static ConnectivityDecision Decide(
        const ResourceRequest& request,
        const ConnectivityConditions& conditions);
};

enum class LinkStatus : uint8_t {
    kStopped = 0,
    kStarting,
    kReady,
    kBackoff,
    kStopping,
    kFailed,
};

enum class LinkResult : uint8_t {
    kOk = 0,
    kUnavailable,
    kBusy,
    kInvalidArgument,
    kPayloadTooLarge,
    kTimeout,
    kTransportError,
};

class ConnectivityLink {
public:
    virtual ~ConnectivityLink() = default;
    virtual LinkResult Start() = 0;
    virtual LinkResult Send(const uint8_t* frame, std::size_t frame_size) = 0;
    virtual void Stop() = 0;
    virtual LinkStatus Status() const = 0;
};

}  // namespace zectrix::companion
