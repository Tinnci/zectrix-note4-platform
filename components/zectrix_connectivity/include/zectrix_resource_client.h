#pragma once

#include "zectrix_resource_gateway.h"
#include "zectrix_wifi_backend.h"

namespace zectrix::connectivity {

struct ResourceResponse {
    uint32_t request_id = 0;
    companion::ResourceStatus status = companion::ResourceStatus::kInvalidResponse;
    companion::ResourceContentType content_type = companion::ResourceContentType::kNone;
    std::array<uint8_t, companion::kResourceMaximumBodySize> body{};
    std::size_t body_size = 0;
    uint32_t retry_after_ms = 0;
    companion::ConnectivityPath path = companion::ConnectivityPath::kDefer;
    WifiOperationResult wifi_operation = WifiOperationResult::kNone;
    WifiStopResult wifi_stop = WifiStopResult::kNotRequired;
    bool retry_queued = false;
};

class PhoneResourceSender {
public:
    virtual ~PhoneResourceSender() = default;
    virtual companion::LinkResult SendResource(
        uint32_t request_id, const companion::ResourceRequestMessage& request) = 0;
};

// The connectivity session owns this coordinator. External callers serialize
// submissions and copied results through ConnectivityService, never the radio.
class ResourceClient final {
public:
    ResourceClient(WifiBackend& wifi, PhoneResourceSender& phone)
        : wifi_(wifi), phone_(phone) {}

    bool Begin(uint32_t request_id, const companion::ResourceRequestMessage& request,
               uint32_t now_ms);
    void Poll(const companion::ConnectivityConditions& conditions, uint32_t now_ms);
    bool AcceptPhoneResponse(uint32_t request_id,
                              const companion::ResourceResponseMessage& response,
                              uint32_t now_ms);
    void PhoneDisconnected(uint32_t now_ms);
    void Cancel(uint32_t now_ms);
    // Retry notices may be superseded while background work continues. A
    // terminal response remains available until the caller consumes it.
    bool TakeResponse(ResourceResponse* response);
    uint32_t NextWakeMs(uint32_t now_ms) const;
    bool Busy() const { return state_ != State::kIdle; }
    bool AwaitingPhone() const { return state_ == State::kPhone; }
    bool WifiBusy() const { return wifi_.Busy(); }
    WifiBackendState WifiState() const { return wifi_.State(); }
    companion::ConnectivityDecision Decision() const { return decision_; }

private:
    enum class State : uint8_t { kIdle, kQueued, kPhone, kWifi, kRetry };
    void CompleteAttempt(uint32_t now_ms);
    void Finish();
    void SetFailure(companion::ResourceStatus status);
    companion::ConnectivityConditions Conditions(
        companion::ConnectivityConditions conditions, uint32_t now_ms) const;

    WifiBackend& wifi_;
    PhoneResourceSender& phone_;
    State state_ = State::kIdle;
    companion::ResourceRequestMessage request_{};
    ResourceResponse response_{};
    WifiBackendOutcome wifi_outcome_{};
    companion::ConnectivityDecision decision_{};
    uint32_t deadline_ms_ = 0;
    uint32_t retry_at_ms_ = 0;
    uint32_t phone_retry_at_ms_ = 0;
    uint32_t wifi_retry_at_ms_ = 0;
    bool retry_waiting_ = false;
    bool phone_backoff_ = false;
    bool wifi_backoff_ = false;
    bool response_ready_ = false;
    bool cancelled_ = false;
};

}  // namespace zectrix::connectivity
