#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "zectrix_wifi_backend.h"

using namespace zectrix::connectivity;

namespace {

class FakeCredentials final : public WifiCredentialSource {
public:
    WifiCredentialResult Load(WifiCredentials* credentials) override {
        ++loads;
        if (result != WifiCredentialResult::kAvailable) return result;
        std::strcpy(credentials->ssid.data(), "test-network");
        std::strcpy(credentials->passphrase.data(), "not-logged-secret");
        return result;
    }

    WifiCredentialResult result = WifiCredentialResult::kAvailable;
    int loads = 0;
};

class FakeDriver final : public WifiBackendDriver {
public:
    WifiDriverResult StartStation(
        const WifiCredentials& credentials) override {
        ++starts;
        saw_credentials = std::strcmp(credentials.ssid.data(),
                                      "test-network") == 0 &&
                          std::strcmp(credentials.passphrase.data(),
                                      "not-logged-secret") == 0;
        return start_result;
    }

    WifiDriverResult PollAssociation() override {
        ++association_polls;
        return association_result;
    }

    WifiDriverResult PollIp() override {
        ++ip_polls;
        return ip_result;
    }

    WifiDriverResult Resolve(
        zectrix::companion::ResourceCapability capability) override {
        ++resolves;
        saw_fixed_capability = capability ==
            zectrix::companion::ResourceCapability::kPublicTestDocumentV1;
        return dns_result;
    }

    WifiDriverResult OpenTls(
        zectrix::companion::ResourceCapability capability) override {
        ++tls_opens;
        saw_fixed_capability = saw_fixed_capability && capability ==
            zectrix::companion::ResourceCapability::kPublicTestDocumentV1;
        return tls_result;
    }

    WifiDriverResult Fetch(
        zectrix::companion::ResourceCapability capability, uint8_t* body,
        std::size_t body_capacity, std::size_t* body_size) override {
        ++fetches;
        saw_fixed_capability = saw_fixed_capability && capability ==
            zectrix::companion::ResourceCapability::kPublicTestDocumentV1;
        if (fetch_result == WifiDriverResult::kReady) {
            assert(body_capacity >= response_size);
            for (std::size_t i = 0; i < response_size; ++i) {
                body[i] = static_cast<uint8_t>(i & 0xff);
            }
            *body_size = response_size;
        }
        return fetch_result;
    }

    WifiDriverResult StopStation() override {
        ++stops;
        return stop_result;
    }

    WifiDriverResult start_result = WifiDriverResult::kReady;
    WifiDriverResult association_result = WifiDriverResult::kReady;
    WifiDriverResult ip_result = WifiDriverResult::kReady;
    WifiDriverResult dns_result = WifiDriverResult::kReady;
    WifiDriverResult tls_result = WifiDriverResult::kReady;
    WifiDriverResult fetch_result = WifiDriverResult::kReady;
    WifiDriverResult stop_result = WifiDriverResult::kReady;
    std::size_t response_size = 3;
    bool saw_credentials = false;
    bool saw_fixed_capability = false;
    int starts = 0;
    int association_polls = 0;
    int ip_polls = 0;
    int resolves = 0;
    int tls_opens = 0;
    int fetches = 0;
    int stops = 0;
};

WifiBackendRequest Request() {
    WifiBackendRequest request{};
    request.maximum_response_bytes = 8;
    request.timeout_ms = 1000;
    return request;
}

WifiBackendOutcome Complete(WifiBackend* backend, uint32_t now_ms = 0) {
    for (int i = 0; i < 20 && backend->Busy(); ++i) {
        backend->Poll(now_ms);
    }
    assert(!backend->Busy());
    WifiBackendOutcome outcome{};
    assert(backend->TakeOutcome(&outcome));
    assert(!backend->TakeOutcome(&outcome));
    return outcome;
}

void TestSuccessfulBurstAndFullStop() {
    FakeCredentials credentials;
    FakeDriver driver;
    WifiBackend backend(&credentials, &driver);

    assert(backend.Begin(Request(), 10));
    assert(!backend.Begin(Request(), 10));
    const WifiBackendOutcome outcome = Complete(&backend, 10);

    assert(outcome.operation == WifiOperationResult::kSuccess);
    assert(outcome.stop == WifiStopResult::kSuccess);
    assert(outcome.body_size == 3);
    assert(outcome.body[0] == 0 && outcome.body[2] == 2);
    assert(credentials.loads == 1);
    assert(driver.starts == 1 && driver.stops == 1);
    assert(driver.saw_credentials && driver.saw_fixed_capability);
    assert(backend.State() == WifiBackendState::kStopped);

    assert(backend.Begin(Request(), 20));
    assert(Complete(&backend, 20).operation == WifiOperationResult::kSuccess);
}

void TestPendingStagesArePolledWithoutRestart() {
    FakeCredentials credentials;
    FakeDriver driver;
    driver.start_result = WifiDriverResult::kPending;
    driver.association_result = WifiDriverResult::kPending;
    WifiBackend backend(&credentials, &driver);
    assert(backend.Begin(Request(), 0));
    backend.Poll(0);
    backend.Poll(0);
    backend.Poll(0);
    backend.Poll(0);
    assert(driver.starts == 1);
    assert(driver.association_polls == 2);
    driver.association_result = WifiDriverResult::kReady;
    const WifiBackendOutcome outcome = Complete(&backend);
    assert(outcome.operation == WifiOperationResult::kSuccess);
    assert(driver.starts == 1 && driver.stops == 1);
}

void TestCredentialFailuresDoNotStartOrStopRadio() {
    for (WifiCredentialResult result : {
             WifiCredentialResult::kUnavailable,
             WifiCredentialResult::kInvalid}) {
        FakeCredentials credentials;
        credentials.result = result;
        FakeDriver driver;
        WifiBackend backend(&credentials, &driver);
        assert(backend.Begin(Request(), 0));
        const WifiBackendOutcome outcome = Complete(&backend);
        assert(outcome.operation ==
               (result == WifiCredentialResult::kUnavailable
                    ? WifiOperationResult::kCredentialsUnavailable
                    : WifiOperationResult::kInvalidCredentials));
        assert(outcome.stop == WifiStopResult::kNotRequired);
        assert(driver.starts == 0 && driver.stops == 0);
    }
}

void TestExplicitFailureMappingAndCleanup() {
    struct FailureCase {
        WifiDriverResult FakeDriver::*field;
        WifiDriverResult driver_result;
        WifiOperationResult operation_result;
    };
    const FailureCase cases[] = {
        {&FakeDriver::association_result, WifiDriverResult::kAuthRejected,
         WifiOperationResult::kAuthRejected},
        {&FakeDriver::ip_result, WifiDriverResult::kIpFailure,
         WifiOperationResult::kIpFailure},
        {&FakeDriver::dns_result, WifiDriverResult::kDnsFailure,
         WifiOperationResult::kDnsFailure},
        {&FakeDriver::tls_result, WifiDriverResult::kTlsFailure,
         WifiOperationResult::kTlsFailure},
        {&FakeDriver::fetch_result, WifiDriverResult::kTransferFailure,
         WifiOperationResult::kTransferFailure},
        {&FakeDriver::fetch_result, WifiDriverResult::kResponseTooLarge,
         WifiOperationResult::kResponseTooLarge},
        {&FakeDriver::fetch_result, WifiDriverResult::kInvalidResponse,
         WifiOperationResult::kInvalidResponse},
    };

    for (const FailureCase& failure : cases) {
        FakeCredentials credentials;
        FakeDriver driver;
        driver.*(failure.field) = failure.driver_result;
        WifiBackend backend(&credentials, &driver);
        assert(backend.Begin(Request(), 0));
        const WifiBackendOutcome outcome = Complete(&backend);
        assert(outcome.operation == failure.operation_result);
        assert(outcome.stop == WifiStopResult::kSuccess);
        assert(outcome.body_size == 0);
        assert(driver.stops == 1);
    }
}

void TestTimeoutCancelAndStopFailureRemainObservable() {
    FakeCredentials credentials;
    FakeDriver driver;
    driver.association_result = WifiDriverResult::kPending;
    driver.stop_result = WifiDriverResult::kTransferFailure;
    WifiBackend backend(&credentials, &driver);
    assert(backend.Begin(Request(), UINT32_MAX - 500));
    backend.Poll(UINT32_MAX - 500);
    backend.Poll(UINT32_MAX - 500);
    backend.Poll(498);
    assert(backend.Busy());
    backend.Poll(499);
    WifiBackendOutcome outcome{};
    assert(backend.TakeOutcome(&outcome));
    assert(outcome.operation == WifiOperationResult::kTimeout);
    assert(outcome.stop == WifiStopResult::kFailure);
    assert(backend.State() == WifiBackendState::kStopFailed);
    assert(!backend.Begin(Request(), 600));

    FakeDriver cancel_driver;
    WifiBackend cancel_backend(&credentials, &cancel_driver);
    assert(cancel_backend.Begin(Request(), 0));
    cancel_backend.Poll(0);
    cancel_backend.Poll(0);
    cancel_backend.Cancel();
    outcome = Complete(&cancel_backend);
    assert(outcome.operation == WifiOperationResult::kCancelled);
    assert(outcome.stop == WifiStopResult::kSuccess);
}

void TestBoundsAndMalformedSuccessfulResponse() {
    FakeCredentials credentials;
    FakeDriver driver;
    WifiBackend backend(&credentials, &driver);
    WifiBackendRequest request = Request();
    request.maximum_response_bytes = 0;
    assert(!backend.Begin(request, 0));
    request.maximum_response_bytes = kMaximumWifiResourceBytes + 1;
    assert(!backend.Begin(request, 0));
    request = Request();
    request.timeout_ms = WifiBackend::kMinimumTimeoutMs - 1;
    assert(!backend.Begin(request, 0));

    request = Request();
    driver.response_size = 0;
    assert(backend.Begin(request, 0));
    assert(Complete(&backend).operation ==
           WifiOperationResult::kInvalidResponse);
}

void TestUnsupportedCapabilityIsTypedWithoutRadioUse() {
    FakeCredentials credentials;
    FakeDriver driver;
    WifiBackend backend(&credentials, &driver);
    WifiBackendRequest request = Request();
    request.capability =
        static_cast<zectrix::companion::ResourceCapability>(0xffff);
    assert(backend.Begin(request, 0));
    WifiBackendOutcome outcome{};
    assert(backend.TakeOutcome(&outcome));
    assert(outcome.operation == WifiOperationResult::kUnsupportedCapability);
    assert(outcome.stop == WifiStopResult::kNotRequired);
    assert(credentials.loads == 0);
    assert(driver.starts == 0 && driver.stops == 0);
}

void TestStopPendingHasAnIndependentDeadline() {
    FakeCredentials credentials;
    FakeDriver driver;
    driver.stop_result = WifiDriverResult::kPending;
    WifiBackend backend(&credentials, &driver);
    assert(backend.Begin(Request(), 100));
    for (int i = 0; i < 8; ++i) backend.Poll(100);
    assert(backend.State() == WifiBackendState::kStopping);
    backend.Poll(2099);
    assert(backend.State() == WifiBackendState::kStopping);
    backend.Poll(2100);
    assert(backend.State() == WifiBackendState::kStopFailed);
    WifiBackendOutcome outcome{};
    assert(backend.TakeOutcome(&outcome));
    assert(outcome.operation == WifiOperationResult::kSuccess);
    assert(outcome.stop == WifiStopResult::kFailure);
}

}  // namespace

int main() {
    TestSuccessfulBurstAndFullStop();
    TestPendingStagesArePolledWithoutRestart();
    TestCredentialFailuresDoNotStartOrStopRadio();
    TestExplicitFailureMappingAndCleanup();
    TestTimeoutCancelAndStopFailureRemainObservable();
    TestBoundsAndMalformedSuccessfulResponse();
    TestUnsupportedCapabilityIsTypedWithoutRadioUse();
    TestStopPendingHasAnIndependentDeadline();
    return 0;
}
