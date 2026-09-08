#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "zectrix_connectivity_policy.h"

namespace zectrix::connectivity {

constexpr std::size_t kMaximumWifiSsidBytes = 32;
constexpr std::size_t kMaximumWifiPassphraseBytes = 64;
constexpr std::size_t kMaximumWifiResourceBytes = 2048;

struct WifiCredentials {
    std::array<char, kMaximumWifiSsidBytes + 1> ssid{};
    std::array<char, kMaximumWifiPassphraseBytes + 1> passphrase{};
};

bool ValidateWifiCredentials(const WifiCredentials& credentials);
void ClearWifiCredentials(WifiCredentials* credentials);

enum class WifiCredentialResult : uint8_t {
    kAvailable = 0,
    kUnavailable,
    kInvalid,
};

class WifiCredentialSource {
public:
    virtual ~WifiCredentialSource() = default;
    virtual WifiCredentialResult Load(WifiCredentials* credentials) = 0;
};

enum class WifiDriverResult : uint8_t {
    kReady = 0,
    kPending,
    kUnavailable,
    kAuthRejected,
    kIpFailure,
    kDnsFailure,
    kTlsFailure,
    kTransferFailure,
    kServerError,
    kResponseTooLarge,
    kInvalidResponse,
};

// This is an internal driver seam. The ESP-IDF implementation owns all
// esp_wifi, esp_netif, DNS and TLS objects below this boundary.
class WifiBackendDriver {
public:
    virtual ~WifiBackendDriver() = default;
    // StartStation begins one station lifecycle. A pending result means that
    // the start was accepted; progress is subsequently reported by
    // PollAssociation. Other operations can be called again while pending.
    virtual WifiDriverResult StartStation(
        const WifiCredentials& credentials) = 0;
    virtual WifiDriverResult PollAssociation() = 0;
    virtual WifiDriverResult PollIp() = 0;
    virtual WifiDriverResult Resolve(
        companion::ResourceCapability capability) = 0;
    virtual WifiDriverResult OpenTls(
        companion::ResourceCapability capability) = 0;
    virtual WifiDriverResult Fetch(
        companion::ResourceCapability capability, uint8_t* body,
        std::size_t body_capacity, std::size_t* body_size) = 0;
    virtual WifiDriverResult StopStation() = 0;
};

enum class WifiBackendState : uint8_t {
    kStopped = 0,
    kLoadingCredentials,
    kStartingStation,
    kAssociating,
    kWaitingForIp,
    kResolving,
    kOpeningTls,
    kTransferring,
    kStopping,
    kStopFailed,
};

enum class WifiOperationResult : uint8_t {
    kNone = 0,
    kSuccess,
    kCredentialsUnavailable,
    kInvalidCredentials,
    kUnavailable,
    kAuthRejected,
    kIpFailure,
    kDnsFailure,
    kTlsFailure,
    kTimeout,
    kTransferFailure,
    kServerError,
    kResponseTooLarge,
    kInvalidResponse,
    kUnsupportedCapability,
    kCancelled,
};

enum class WifiStopResult : uint8_t {
    kNotRequired = 0,
    kSuccess,
    kFailure,
};

struct WifiBackendRequest {
    companion::ResourceCapability capability =
        companion::ResourceCapability::kPublicTestDocumentV1;
    std::size_t maximum_response_bytes = kMaximumWifiResourceBytes;
    uint32_t timeout_ms = 15000;
};

struct WifiBackendOutcome {
    WifiOperationResult operation = WifiOperationResult::kNone;
    WifiStopResult stop = WifiStopResult::kNotRequired;
    std::array<uint8_t, kMaximumWifiResourceBytes> body{};
    std::size_t body_size = 0;
};

class WifiBackend {
public:
    static constexpr uint32_t kMinimumTimeoutMs = 1000;
    static constexpr uint32_t kMaximumTimeoutMs = 60000;
    static constexpr uint32_t kStopTimeoutMs = 2000;

    WifiBackend(WifiCredentialSource* credentials, WifiBackendDriver* driver);
    ~WifiBackend();

    WifiBackend(const WifiBackend&) = delete;
    WifiBackend& operator=(const WifiBackend&) = delete;

    bool Begin(const WifiBackendRequest& request, uint32_t now_ms);
    void Poll(uint32_t now_ms);
    void Cancel();
    bool TakeOutcome(WifiBackendOutcome* outcome);

    WifiBackendState State() const { return state_; }
    bool Busy() const {
        return state_ != WifiBackendState::kStopped &&
               state_ != WifiBackendState::kStopFailed;
    }

private:
    static bool DeadlineReached(uint32_t now_ms, uint32_t deadline_ms);
    void Fail(WifiOperationResult result);
    void ProcessDriverResult(WifiDriverResult result,
                             WifiBackendState success_state);
    void BeginStopping();
    void StopNow(uint32_t now_ms);

    WifiCredentialSource* credentials_ = nullptr;
    WifiBackendDriver* driver_ = nullptr;
    WifiBackendState state_ = WifiBackendState::kStopped;
    WifiBackendRequest request_{};
    WifiCredentials loaded_credentials_{};
    WifiBackendOutcome outcome_{};
    uint32_t deadline_ms_ = 0;
    uint32_t stop_deadline_ms_ = 0;
    uint32_t current_ms_ = 0;
    bool station_started_ = false;
    bool outcome_ready_ = false;
};

}  // namespace zectrix::connectivity
