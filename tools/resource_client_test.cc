#include "zectrix_resource_client.h"

#include <cassert>
#include <cstring>

using namespace zectrix::companion;
using namespace zectrix::connectivity;

namespace {

class Credentials final : public WifiCredentialSource {
public:
    WifiCredentialResult Load(WifiCredentials* credentials) override {
        std::strcpy(credentials->ssid.data(), "test-network");
        return WifiCredentialResult::kAvailable;
    }
};

class Radio final : public WifiBackendDriver {
public:
    WifiDriverResult StartStation(const WifiCredentials&) override {
        ++starts;
        return WifiDriverResult::kPending;
    }
    WifiDriverResult PollAssociation() override { return WifiDriverResult::kReady; }
    WifiDriverResult PollIp() override { return WifiDriverResult::kReady; }
    WifiDriverResult Resolve(ResourceCapability) override { return WifiDriverResult::kReady; }
    WifiDriverResult OpenTls(ResourceCapability) override { return WifiDriverResult::kReady; }
    WifiDriverResult Fetch(ResourceCapability, uint8_t* body,
                           std::size_t capacity, std::size_t* size) override {
        ++fetches;
        assert(capacity >= 3);
        if (fetch_result == WifiDriverResult::kReady) {
            std::memcpy(body, "ok\n", 3);
            *size = 3;
        }
        return fetch_result;
    }
    WifiDriverResult StopStation() override {
        ++stops;
        return stop_result;
    }
    int starts = 0;
    int fetches = 0;
    int stops = 0;
    WifiDriverResult fetch_result = WifiDriverResult::kReady;
    WifiDriverResult stop_result = WifiDriverResult::kReady;
};

class Phone final : public PhoneResourceSender {
public:
    LinkResult SendResource(uint32_t id, const ResourceRequestMessage& request) override {
        ++sends;
        request_id = id;
        last_request = request;
        return send_result;
    }
    int sends = 0;
    uint32_t request_id = 0;
    ResourceRequestMessage last_request{};
    LinkResult send_result = LinkResult::kOk;
};

struct Fixture {
    Credentials credentials;
    Radio radio;
    Phone phone;
    WifiBackend wifi{&credentials, &radio};
    ResourceClient client{wifi, phone};
    ConnectivityConditions conditions{};
    ResourceRequestMessage request{};

    Fixture() {
        conditions.phone = PhoneAvailability::kConnected;
        conditions.wifi_credentials_available = true;
        request.durable = false;
        request.maximum_response_bytes = 8;
        request.timeout_ms = 1000;
    }
    void Begin(uint32_t now = 0) {
        assert(client.Begin(42, request, now));
        client.Poll(conditions, now);
    }
    void Reach(WifiBackendState state, uint32_t now = 0) {
        for (int count = 0; count < 16 && wifi.State() != state; ++count) {
            client.Poll(conditions, now);
        }
        assert(wifi.State() == state);
    }
    ResourceResponse Complete(uint32_t now = 0) {
        ResourceResponse response;
        for (int count = 0; count < 16; ++count) {
            client.Poll(conditions, now);
            if (client.TakeResponse(&response)) {
                assert(response.request_id == 42);
                return response;
            }
        }
        assert(false);
        return {};
    }
};

void TestPhonePreferredAndCopiedResult() {
    Fixture f;
    f.Begin();
    assert(f.phone.sends == 1 && f.radio.starts == 0);
    assert(f.client.NextWakeMs(100) == 900);
    assert(!f.client.Begin(43, f.request, 0));
    uint8_t body[] = {'o', 'k'};
    ResourceResponseMessage reply{ResourceStatus::kSuccess,
        ResourceContentType::kTextPlainUtf8, body, sizeof(body), 0};
    assert(!f.client.AcceptPhoneResponse(41, reply, 10));
    assert(f.client.AcceptPhoneResponse(42, reply, 10));
    body[0] = 'x';
    assert(!f.client.Begin(43, f.request, 10));
    auto response = f.Complete(10);
    assert(response.status == ResourceStatus::kSuccess);
    assert(response.path == ConnectivityPath::kPhoneProxy);
    assert(response.body_size == 2 && response.body[0] == 'o');
    assert(!f.client.Busy() && f.client.NextWakeMs(10) == UINT32_MAX);
    assert(!f.client.TakeResponse(&response));
    assert(f.client.Begin(43, f.request, 11));
}

void TestDirectResultWaitsForRadioStop() {
    Fixture f;
    f.conditions.phone = PhoneAvailability::kUnavailable;
    f.radio.stop_result = WifiDriverResult::kPending;
    f.Begin();
    f.Reach(WifiBackendState::kStopping);
    ResourceResponse response;
    assert(!f.client.TakeResponse(&response));
    f.client.Poll(f.conditions, 100);
    assert(f.client.Busy() && !f.client.TakeResponse(&response));
    f.radio.stop_result = WifiDriverResult::kReady;
    response = f.Complete(101);
    assert(response.status == ResourceStatus::kSuccess);
    assert(response.path == ConnectivityPath::kDirectWifi);
    assert(response.wifi_operation == WifiOperationResult::kSuccess);
    assert(response.wifi_stop == WifiStopResult::kSuccess);
    assert(response.body_size == 3 && std::memcmp(response.body.data(), "ok\n", 3) == 0);
    assert(f.radio.starts == 1 && f.phone.sends == 0 && !f.client.WifiBusy());
}

void TestPhoneFailuresEscalate() {
    for (int failure = 0; failure < 4; ++failure) {
        Fixture f;
        if (failure == 0) f.phone.send_result = LinkResult::kUnavailable;
        f.Begin();
        uint32_t now = 10;
        if (failure == 1) {
            ResourceResponseMessage offline{};
            offline.status = ResourceStatus::kPhoneOffline;
            assert(f.client.AcceptPhoneResponse(42, offline, now));
        } else if (failure == 2) {
            f.conditions.phone = PhoneAvailability::kUnavailable;
        } else if (failure == 3) {
            now = 1000;
        }
        const auto response = f.Complete(now);
        assert(response.status == ResourceStatus::kSuccess);
        assert(response.path == ConnectivityPath::kDirectWifi);
        assert(f.phone.sends == 1 && f.radio.starts == 1 && f.radio.stops == 1);
        assert(!f.client.AcceptPhoneResponse(42, {}, now));
    }
}

void TestStrictPolicyAndPower() {
    for (int unavailable = 0; unavailable < 5; ++unavailable) {
        Fixture f;
        f.conditions.phone = PhoneAvailability::kUnavailable;
        if (unavailable == 0) f.conditions.user_policy = UserConnectivityPolicy::kOffline;
        if (unavailable == 1) f.conditions.user_policy = UserConnectivityPolicy::kPhoneOnly;
        if (unavailable == 2) f.conditions.battery_percent = 19;
        if (unavailable == 3) f.conditions.wifi_credentials_available = false;
        if (unavailable == 4) f.conditions.power_state = ProductPowerState::kShutdown;
        f.Begin();
        const auto response = f.Complete();
        assert(response.status == ResourceStatus::kPhoneUnavailable);
        assert(!response.retry_queued && !f.client.Busy());
        assert(f.phone.sends == 0 && f.radio.starts == 0);
    }
    Fixture f;
    f.conditions.user_policy = UserConnectivityPolicy::kWifiOnly;
    f.conditions.battery_percent = 0;
    f.conditions.external_power = true;
    f.Begin();
    assert(f.Complete().path == ConnectivityPath::kDirectWifi);
    assert(f.phone.sends == 0);
}

void TestPolicyChangesDuringTransfers() {
    Fixture f;
    f.Begin();
    f.conditions.user_policy = UserConnectivityPolicy::kWifiOnly;
    assert(f.Complete(10).path == ConnectivityPath::kDirectWifi);

    Fixture g;
    g.conditions.user_policy = UserConnectivityPolicy::kWifiOnly;
    g.radio.fetch_result = WifiDriverResult::kPending;
    g.Begin();
    g.Reach(WifiBackendState::kTransferring);
    g.conditions.user_policy = UserConnectivityPolicy::kPhoneOnly;
    g.client.Poll(g.conditions, 10);
    assert(g.radio.stops == 1 && g.client.AwaitingPhone());
    assert(g.phone.sends == 1 && !g.client.WifiBusy());
}

void TestDurableRetryWithoutReadingProgress() {
    Fixture f;
    f.request.durable = true;
    f.radio.fetch_result = WifiDriverResult::kTransferFailure;
    f.Begin();
    ResourceResponseMessage offline{};
    offline.status = ResourceStatus::kPhoneOffline;
    offline.retry_after_ms = 1000;
    assert(f.client.AcceptPhoneResponse(42, offline, 0));
    for (int count = 0; count < 16; ++count) f.client.Poll(f.conditions, 0);
    assert(f.client.Busy());
    assert(f.client.NextWakeMs(0) == 1000);
    f.client.Poll(f.conditions, 999);
    assert(f.phone.sends == 1);
    // The application can be in another screen while the request retries.
    f.client.Poll(f.conditions, 1000);
    assert(f.phone.sends == 2 && f.client.AwaitingPhone());
    assert(f.phone.request_id == 42 && f.phone.last_request.durable);
    assert(f.phone.last_request.timeout_ms == f.request.timeout_ms);
    f.client.Cancel(1001);
    assert(!f.Complete(1001).retry_queued);
}

void TestDeferredWorkReactsToNewPath() {
    Fixture f;
    f.request.durable = true;
    f.conditions.phone = PhoneAvailability::kUnavailable;
    f.conditions.wifi_credentials_available = false;
    f.Begin();
    ResourceResponse response;
    assert(f.client.TakeResponse(&response) && response.retry_queued);
    f.conditions.wifi_credentials_available = true;
    assert(f.Complete(10).status == ResourceStatus::kSuccess);
}

void TestCancellationAndStopFailure() {
    for (int cause = 0; cause < 3; ++cause) {
        Fixture f;
        f.request.durable = true;
        f.conditions.phone = PhoneAvailability::kUnavailable;
        f.radio.fetch_result = WifiDriverResult::kPending;
        f.radio.stop_result = WifiDriverResult::kPending;
        f.Begin();
        f.Reach(WifiBackendState::kTransferring);
        if (cause == 0) f.client.Cancel(20);
        if (cause == 1) f.conditions.user_policy = UserConnectivityPolicy::kOffline;
        if (cause == 2) f.conditions.power_state = ProductPowerState::kShutdown;
        f.client.Poll(f.conditions, 20);
        ResourceResponse response;
        assert(f.client.WifiBusy() && !f.client.TakeResponse(&response));
        f.radio.stop_result = WifiDriverResult::kReady;
        response = f.Complete(21);
        assert(response.wifi_operation == WifiOperationResult::kCancelled);
        assert(response.wifi_stop == WifiStopResult::kSuccess);
        assert(!response.retry_queued && !f.client.Busy());
    }
    Fixture f;
    f.conditions.phone = PhoneAvailability::kUnavailable;
    f.radio.stop_result = WifiDriverResult::kPending;
    f.Begin();
    f.Reach(WifiBackendState::kStopping);
    const auto response = f.Complete(WifiBackend::kStopTimeoutMs);
    assert(response.status == ResourceStatus::kSuccess);
    assert(response.wifi_stop == WifiStopResult::kFailure);
    f.Begin(3000);
    assert(f.Complete(3000).status == ResourceStatus::kPhoneUnavailable);
    assert(f.radio.starts == 1);
}

void TestTerminalErrorsAndTimerWrap() {
    Fixture f;
    f.Begin();
    ResourceResponseMessage response{};
    response.status = ResourceStatus::kServerError;
    assert(f.client.AcceptPhoneResponse(42, response, 10));
    assert(f.Complete(10).status == ResourceStatus::kServerError);
    assert(f.radio.starts == 0);

    Fixture g;
    constexpr uint32_t start = UINT32_MAX - 500;
    g.Begin(start);
    g.client.Poll(g.conditions, 498);
    assert(g.client.AwaitingPhone());
    assert(g.Complete(499).path == ConnectivityPath::kDirectWifi);

    Fixture h;
    h.request.capability = static_cast<ResourceCapability>(99);
    h.Begin();
    assert(h.Complete().status == ResourceStatus::kUnsupportedCapability);
    assert(h.radio.starts == 0 && h.phone.sends == 0);
}

}  // namespace

int main() {
    TestPhonePreferredAndCopiedResult();
    TestDirectResultWaitsForRadioStop();
    TestPhoneFailuresEscalate();
    TestStrictPolicyAndPower();
    TestPolicyChangesDuringTransfers();
    TestDurableRetryWithoutReadingProgress();
    TestDeferredWorkReactsToNewPath();
    TestCancellationAndStopFailure();
    TestTerminalErrorsAndTimerWrap();
}
