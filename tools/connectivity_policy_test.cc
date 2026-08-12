#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "zectrix_connectivity_policy.h"

using namespace zectrix::companion;

namespace {

ConnectivityDecision Decide(std::size_t size,
                            const ConnectivityConditions& conditions,
                            uint32_t deadline = 30000, bool durable = true) {
    ResourceRequest request{};
    request.expected_response_size = size;
    request.deadline_ms = deadline;
    request.durable = durable;
    return ConnectivityPolicy::Decide(request, conditions);
}

void TestAutomaticPolicy() {
    ConnectivityConditions conditions{};
    conditions.phone = PhoneAvailability::kConnected;
    assert(Decide(512, conditions).path == ConnectivityPath::kPhoneProxy);

    conditions.wifi_credentials_available = true;
    assert(Decide(3000, conditions).path == ConnectivityPath::kDirectWifi);
    assert(Decide(512, conditions, 1000).path ==
           ConnectivityPath::kPhoneProxy);

    conditions.phone = PhoneAvailability::kUnavailable;
    auto decision = Decide(512, conditions);
    assert(decision.path == ConnectivityPath::kDirectWifi);
    assert(decision.reason == DecisionReason::kPhoneUnavailable);

    conditions.battery_percent = 19;
    decision = Decide(512, conditions);
    assert(decision.path == ConnectivityPath::kDefer);
    assert(decision.reason == DecisionReason::kInsufficientPower);
    conditions.external_power = true;
    assert(Decide(512, conditions).path == ConnectivityPath::kDirectWifi);

    conditions.external_power = false;
    conditions.battery_percent = 100;
    conditions.phone = PhoneAvailability::kPresent;
    decision = Decide(512, conditions, 30000, true);
    assert(decision.path == ConnectivityPath::kDefer);
    assert(decision.reason == DecisionReason::kWaitForPhone);
    assert(Decide(512, conditions, 1000, true).path ==
           ConnectivityPath::kDirectWifi);
}

void TestOverridesAndBackoff() {
    ConnectivityConditions conditions{};
    conditions.phone = PhoneAvailability::kConnected;
    conditions.wifi_credentials_available = true;
    conditions.user_policy = UserConnectivityPolicy::kOffline;
    assert(Decide(1, conditions).reason == DecisionReason::kUserOffline);

    conditions.user_policy = UserConnectivityPolicy::kPhoneOnly;
    assert(Decide(4096, conditions).reason ==
           DecisionReason::kUserForcedPhone);
    conditions.phone_retry_after_ms = 500;
    auto decision = Decide(1, conditions);
    assert(decision.path == ConnectivityPath::kDefer);
    assert(decision.retry_after_ms == 500);

    conditions.user_policy = UserConnectivityPolicy::kWifiOnly;
    conditions.phone_retry_after_ms = 0;
    assert(Decide(1, conditions).reason == DecisionReason::kUserForcedWifi);
    conditions.wifi_retry_after_ms = 900;
    decision = Decide(1, conditions);
    assert(decision.reason == DecisionReason::kWifiBackoff);
    assert(decision.retry_after_ms == 900);

    conditions.user_policy = UserConnectivityPolicy::kAutomatic;
    conditions.phone_retry_after_ms = 700;
    decision = Decide(1, conditions);
    assert(decision.path == ConnectivityPath::kDefer);
    assert(decision.retry_after_ms == 700);

    conditions.power_state = ProductPowerState::kShutdown;
    assert(Decide(1, conditions).reason == DecisionReason::kShuttingDown);
    conditions.power_state = ProductPowerState::kActive;
    assert(Decide(0, conditions).reason == DecisionReason::kInvalidRequest);
    assert(Decide(4097, conditions).reason == DecisionReason::kInvalidRequest);
}

class FakeLink final : public ConnectivityLink {
public:
    LinkResult Start() override {
        ++starts;
        status = LinkStatus::kReady;
        return LinkResult::kOk;
    }
    LinkResult Send(const uint8_t* frame, std::size_t frame_size) override {
        if (status != LinkStatus::kReady) return LinkResult::kUnavailable;
        if (frame == nullptr || frame_size == 0) {
            return LinkResult::kInvalidArgument;
        }
        sent += frame_size;
        return LinkResult::kOk;
    }
    void Stop() override {
        ++stops;
        status = LinkStatus::kStopped;
    }
    LinkStatus Status() const override { return status; }

    LinkStatus status = LinkStatus::kStopped;
    int starts = 0;
    int stops = 0;
    std::size_t sent = 0;
};

void TestTransportNeutralLink() {
    FakeLink phone;
    FakeLink wifi;
    ConnectivityLink* links[] = {&phone, &wifi};
    const uint8_t frame[] = {1, 2, 3};
    for (ConnectivityLink* link : links) {
        assert(link->Start() == LinkResult::kOk);
        assert(link->Send(frame, sizeof(frame)) == LinkResult::kOk);
        link->Stop();
        assert(link->Status() == LinkStatus::kStopped);
    }
    assert(phone.sent == sizeof(frame) && wifi.sent == sizeof(frame));
}

}  // namespace

int main() {
    TestAutomaticPolicy();
    TestOverridesAndBackoff();
    TestTransportNeutralLink();
    return 0;
}
