#include "zectrix_wifi_esp_driver.h"
#include "zectrix_wifi_http.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "lwip/dns.h"
#include "lwip/tcpip.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct WifiHostHandler { esp_event_base_t base; esp_event_handler_t callback; void* context; };

namespace {
using namespace zectrix::connectivity;
constexpr auto capability = zectrix::companion::ResourceCapability::kPublicTestDocumentV1;
constexpr ip_addr_t resolved_address{0xc0000207, true};
std::mutex event_mutex, dns_mutex;
std::vector<std::unique_ptr<WifiHostHandler>> handlers;
struct TcpCall { tcpip_callback_fn callback; void* context; };
struct DnsCall { dns_found_callback callback; void* context; };
std::deque<TcpCall> tcp_calls;
std::deque<DnsCall> dns_calls;
std::atomic<unsigned> netifs{0}, tls_objects{0}, lookups{0};
std::atomic<bool> unregister_entered{false};
esp_netif_t* station_netif = nullptr;
bool wifi_initialized = false, wifi_started = false, default_handlers = false;
bool fail_init = false, fail_stop = false, fail_enqueue = false;
bool fail_deinit = false;
esp_event_base_t fail_unregister_base = nullptr;
std::vector<std::string> cleanup;
WifiDriverResult http_result = WifiDriverResult::kPending;
bool http_active = false;
unsigned register_calls = 0, fail_register_at = 0, connect_calls = 0;
err_t dns_result = ERR_INPROGRESS;

template <typename Predicate>
void WaitFor(Predicate ready) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!ready()) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
}

void Reset() {
    assert(handlers.empty() && tcp_calls.empty() && dns_calls.empty());
    assert(netifs == 0 && tls_objects == 0 && station_netif == nullptr);
    assert(!wifi_initialized && !wifi_started && !default_handlers && !http_active);
    unregister_entered = false;
    fail_init = fail_stop = fail_enqueue = false;
    register_calls = fail_register_at = connect_calls = 0;
    lookups = 0;
    dns_result = ERR_INPROGRESS;
    fail_deinit = false;
    fail_unregister_base = nullptr;
    http_result = WifiDriverResult::kPending;
    cleanup.clear();
}

void Emit(esp_event_base_t base, int32_t event, void* data = nullptr) {
    // IDF serializes dispatch and unregister with the event-loop mutex.
    std::lock_guard<std::mutex> lock(event_mutex);
    for (const auto& handler : handlers) {
        if (handler->base == base) handler->callback(handler->context, base, event, data);
    }
}

WifiCredentials Credentials() {
    WifiCredentials credentials;
    std::strcpy(credentials.ssid.data(), "test-ap");
    std::strcpy(credentials.passphrase.data(), "test-password");
    return credentials;
}

void Connect(EspWifiBackendDriver& driver) {
    assert(driver.StartStation(Credentials()) == WifiDriverResult::kPending);
    assert(driver.PollAssociation() == WifiDriverResult::kPending);
    Emit(WIFI_EVENT, WIFI_EVENT_STA_START);
    assert(driver.PollAssociation() == WifiDriverResult::kPending);
    Emit(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED);
    assert(driver.PollAssociation() == WifiDriverResult::kReady);
    ip_event_got_ip_t event{station_netif, {{1}}};
    Emit(IP_EVENT, IP_EVENT_STA_GOT_IP, &event);
    assert(driver.PollIp() == WifiDriverResult::kReady);
}

void StartDns() {
    TcpCall call;
    {
        std::lock_guard<std::mutex> lock(dns_mutex);
        assert(!tcp_calls.empty());
        call = tcp_calls.front();
        tcp_calls.pop_front();
    }
    call.callback(call.context);
}

void CompleteDns(bool success = true) {
    DnsCall call;
    {
        std::lock_guard<std::mutex> lock(dns_mutex);
        assert(!dns_calls.empty());
        call = dns_calls.front();
        dns_calls.pop_front();
    }
    call.callback("zectrix.com", success ? &resolved_address : nullptr, call.context);
}

void TestExclusiveClaimAndStartupFailure() {
    for (unsigned failure = 0; failure < 3; ++failure) {
        Reset();
        EspWifiBackendDriver first, second;
        fail_init = failure == 1;
        fail_register_at = failure == 2 ? 2 : 0;
        assert(first.StartStation(Credentials()) == WifiDriverResult::kPending);
        assert(second.StartScan() == WifiDriverResult::kUnavailable);
        if (failure != 0) assert(first.PollAssociation() == WifiDriverResult::kUnavailable);
        else {
            fail_stop = true;
            assert(first.StopStation() == WifiDriverResult::kUnavailable);
            assert(second.StartScan() == WifiDriverResult::kUnavailable);
            fail_stop = false;
        }
        assert(first.StopStation() == WifiDriverResult::kReady);
        fail_init = false;
        fail_register_at = 0;
        assert(second.StartScan() == WifiDriverResult::kPending);
        Emit(WIFI_EVENT, WIFI_EVENT_STA_START);
        WifiScanSnapshot scan;
        assert(second.PollScan("test-ap", &scan) == WifiDriverResult::kPending);
        Emit(WIFI_EVENT, WIFI_EVENT_SCAN_DONE);
        assert(second.PollScan("test-ap", &scan) == WifiDriverResult::kReady);
        assert(scan.target_found && scan.access_point_count == 1 && scan.best_rssi == -42);
        assert(second.StopStation() == WifiDriverResult::kReady);
    }
    Reset();
}

void TestConcurrentEventsAndTeardown() {
    Reset();
    auto driver = std::make_unique<EspWifiBackendDriver>();
    Connect(*driver);
    assert(connect_calls == 1);
    ip_event_got_ip_t event{station_netif, {{1}}};
    std::atomic<bool> complete{false};
    std::thread publisher([&] {
        for (unsigned index = 0; index < 2000; ++index) {
            Emit(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED);
            Emit(IP_EVENT, IP_EVENT_STA_GOT_IP, &event);
        }
        complete = true;
    });
    WaitFor([&] {
        assert(driver->PollAssociation() == WifiDriverResult::kReady);
        assert(driver->PollIp() == WifiDriverResult::kReady);
        return complete.load();
    });
    publisher.join();
    wifi_event_sta_disconnected_t disconnected{WIFI_REASON_AUTH_FAIL};
    Emit(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnected);
    assert(driver->PollAssociation() == WifiDriverResult::kAuthRejected);

    std::atomic<bool> entered{false};
    std::thread late_event([&] {
        std::lock_guard<std::mutex> lock(event_mutex);
        entered = true;
        WaitFor([] { return unregister_entered.load(); });
        assert(netifs == 1);
        for (const auto& handler : handlers) {
            if (handler->base == IP_EVENT) {
                handler->callback(handler->context, IP_EVENT, IP_EVENT_STA_GOT_IP, &event);
            }
        }
    });
    WaitFor([&] { return entered.load(); });
    driver.reset();
    late_event.join();
    Emit(WIFI_EVENT, WIFI_EVENT_STA_START);
    Reset();
}

void TestDnsCancellationAndReuse() {
    for (bool started : {false, true}) {
        Reset();
        auto driver = std::make_unique<EspWifiBackendDriver>();
        Connect(*driver);
        assert(driver->Resolve(capability) == WifiDriverResult::kPending);
        if (started) StartDns();
        driver.reset();
        std::thread late_callback([=] { if (started) CompleteDns(); else StartDns(); });
        late_callback.join();
        assert(lookups == (started ? 1u : 0u));
        Reset();
    }

    EspWifiBackendDriver driver;
    Connect(driver);
    assert(driver.Resolve(capability) == WifiDriverResult::kPending);
    StartDns();
    assert(driver.StopStation() == WifiDriverResult::kReady);
    Connect(driver);
    assert(driver.Resolve(capability) == WifiDriverResult::kPending);
    StartDns();
    // A previous burst's late reply must not complete the new DNS operation.
    CompleteDns();
    assert(driver.Resolve(capability) == WifiDriverResult::kPending);
    std::thread answer([] { CompleteDns(); });
    WifiDriverResult result = WifiDriverResult::kPending;
    WaitFor([&] { result = driver.Resolve(capability); return result != WifiDriverResult::kPending; });
    assert(result == WifiDriverResult::kReady);
    assert(driver.OpenTls(capability) == WifiDriverResult::kReady);
    answer.join();
    assert(driver.StopStation() == WifiDriverResult::kReady);
    Reset();
}

void TestDnsFailuresAndCachedAnswer() {
    for (unsigned mode = 0; mode < 4; ++mode) {
        Reset();
        EspWifiBackendDriver driver;
        Connect(driver);
        fail_enqueue = mode == 0;
        dns_result = mode == 1 ? ERR_MEM : mode == 2 ? ERR_OK : ERR_INPROGRESS;
        const auto initial = driver.Resolve(capability);
        if (mode == 0) assert(initial == WifiDriverResult::kDnsFailure);
        else {
            assert(initial == WifiDriverResult::kPending);
            StartDns();
            if (mode == 3) CompleteDns(false);
            assert(driver.Resolve(capability) == (mode == 2 ? WifiDriverResult::kReady
                                                            : WifiDriverResult::kDnsFailure));
            if (mode == 2) assert(driver.OpenTls(capability) == WifiDriverResult::kReady);
        }
        assert(driver.StopStation() == WifiDriverResult::kReady);
    }
    Reset();
}

class StoredCredentials final : public WifiCredentialSource {
public:
    WifiCredentialResult Load(WifiCredentials* output) override {
        *output = Credentials();
        return WifiCredentialResult::kAvailable;
    }
};

void StartBurst(WifiBackend& backend) {
    assert(backend.Begin({}, 0));
    backend.Poll(0);
    backend.Poll(1);
    Emit(WIFI_EVENT, WIFI_EVENT_STA_START);
    Emit(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED);
    ip_event_got_ip_t event{station_netif, {{1}}};
    Emit(IP_EVENT, IP_EVENT_STA_GOT_IP, &event);
    backend.Poll(2);
    backend.Poll(3);
    backend.Poll(4);
    StartDns();
    CompleteDns();
    backend.Poll(5);
    backend.Poll(6);
    backend.Poll(7);
    assert(backend.State() == WifiBackendState::kTransferring);
    assert(http_active && wifi_started && tls_objects == 1);
}

void TestBurstPowersDownBeforeResult() {
    const WifiOperationResult expected[] = {
        WifiOperationResult::kSuccess, WifiOperationResult::kTransferFailure,
        WifiOperationResult::kInvalidResponse, WifiOperationResult::kTimeout,
        WifiOperationResult::kCancelled,
    };
    for (unsigned mode = 0; mode < 5; ++mode) {
        Reset();
        StoredCredentials credentials;
        EspWifiBackendDriver driver, diagnostic;
        WifiBackend backend(&credentials, &driver);
        StartBurst(backend);
        assert(diagnostic.StartScan() == WifiDriverResult::kUnavailable);
        WifiBackendOutcome outcome;
        assert(!backend.TakeOutcome(&outcome));
        if (mode == 3) backend.Poll(15000);
        else {
            if (mode == 4) backend.Cancel();
            else {
                http_result = mode == 0 ? WifiDriverResult::kReady
                    : mode == 1 ? WifiDriverResult::kTransferFailure
                                : WifiDriverResult::kInvalidResponse;
                backend.Poll(8);
            }
            assert(backend.State() == WifiBackendState::kStopping);
            assert(wifi_started && !backend.TakeOutcome(&outcome));
            backend.Poll(9);
        }
        assert(backend.State() == WifiBackendState::kStopped);
        assert(!http_active && tls_objects == 0 && netifs == 0 && handlers.empty());
        assert(!wifi_started && !wifi_initialized && !default_handlers);
        assert((cleanup == std::vector<std::string>{"http", "tls", "wifi-stop",
            "wifi-deinit", "wifi-handler", "ip-handler", "default-handlers", "netif"}));
        assert(backend.TakeOutcome(&outcome));
        assert(outcome.operation == expected[mode] && outcome.stop == WifiStopResult::kSuccess);
        assert(outcome.body_size == (mode == 0 ? 5u : 0u));
        assert(diagnostic.StartScan() == WifiDriverResult::kPending);
        assert(diagnostic.StopStation() == WifiDriverResult::kReady);
    }
    Reset();
}

void TestFailedCleanupRetainsOwnership() {
    for (unsigned phase = 0; phase < 4; ++phase) {
        Reset();
        StoredCredentials credentials;
        EspWifiBackendDriver driver, diagnostic;
        WifiBackend backend(&credentials, &driver);
        StartBurst(backend);
        http_result = WifiDriverResult::kReady;
        backend.Poll(8);
        fail_stop = phase == 0;
        fail_deinit = phase == 1;
        fail_unregister_base = phase == 2 ? WIFI_EVENT : phase == 3 ? IP_EVENT : nullptr;
        backend.Poll(9);
        WifiBackendOutcome outcome;
        assert(backend.State() == WifiBackendState::kStopFailed);
        assert(backend.TakeOutcome(&outcome));
        assert(outcome.operation == WifiOperationResult::kSuccess && outcome.stop == WifiStopResult::kFailure);
        assert(!backend.Begin({}, 10));
        assert(!http_active && tls_objects == 0 && netifs == 1);
        assert(diagnostic.StartScan() == WifiDriverResult::kUnavailable);
        fail_stop = fail_deinit = false;
        fail_unregister_base = nullptr;
        assert(driver.StopStation() == WifiDriverResult::kReady);
        assert(netifs == 0 && handlers.empty() && !wifi_initialized);
        assert(diagnostic.StartScan() == WifiDriverResult::kPending);
        assert(diagnostic.StopStation() == WifiDriverResult::kReady);
    }
    Reset();
}
}  // namespace

esp_err_t esp_event_loop_create_default() { return ESP_OK; }
esp_err_t esp_event_handler_instance_register(esp_event_base_t base, int32_t,
                                              esp_event_handler_t callback, void* context,
                                              esp_event_handler_instance_t* output) {
    if (++register_calls == fail_register_at) return ESP_FAIL;
    std::lock_guard<std::mutex> lock(event_mutex);
    auto handler = std::make_unique<WifiHostHandler>(WifiHostHandler{base, callback, context});
    *output = handler.get();
    handlers.push_back(std::move(handler));
    return ESP_OK;
}
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base, int32_t,
                                                esp_event_handler_instance_t handler) {
    unregister_entered = true;
    std::lock_guard<std::mutex> lock(event_mutex);
    if (base == fail_unregister_base) return ESP_FAIL;
    const auto found = std::find_if(handlers.begin(), handlers.end(),
                                   [=](const auto& item) { return item.get() == handler; });
    assert(found != handlers.end());
    handlers.erase(found);
    cleanup.emplace_back(base == WIFI_EVENT ? "wifi-handler" : "ip-handler");
    return ESP_OK;
}
esp_err_t esp_netif_init() { return ESP_OK; }
esp_netif_t* esp_netif_new(const esp_netif_config_t*) {
    assert(station_netif == nullptr);
    ++netifs;
    return station_netif = new esp_netif_t;
}
void esp_netif_destroy(esp_netif_t* netif) {
    assert(handlers.empty() && !default_handlers && !wifi_initialized && !wifi_started);
    assert(netif == station_netif);
    station_netif = nullptr;
    --netifs;
    cleanup.emplace_back("netif");
    delete netif;
}
esp_err_t esp_netif_attach_wifi_station(esp_netif_t*) { return ESP_OK; }
esp_err_t esp_wifi_set_default_wifi_sta_handlers() { default_handlers = true; return ESP_OK; }
esp_err_t esp_wifi_clear_default_wifi_driver_and_handlers(esp_netif_t*) {
    default_handlers = false;
    cleanup.emplace_back("default-handlers");
    return ESP_OK;
}
esp_err_t esp_wifi_init(const wifi_init_config_t*) {
    assert(!wifi_initialized);
    wifi_initialized = !fail_init;
    return fail_init ? ESP_FAIL : ESP_OK;
}
esp_err_t esp_wifi_deinit() {
    assert(wifi_initialized && !wifi_started);
    if (fail_deinit) return ESP_FAIL;
    wifi_initialized = false;
    cleanup.emplace_back("wifi-deinit");
    return ESP_OK;
}
esp_err_t esp_wifi_set_storage(int) { return ESP_OK; }
esp_err_t esp_wifi_set_mode(int) { return ESP_OK; }
esp_err_t esp_wifi_set_config(int, const wifi_config_t*) { return ESP_OK; }
esp_err_t esp_wifi_start() { assert(wifi_initialized); wifi_started = true; return ESP_OK; }
esp_err_t esp_wifi_stop() {
    assert(!http_active && tls_objects == 0);
    if (fail_stop) return ESP_FAIL;
    wifi_started = false;
    cleanup.emplace_back("wifi-stop");
    return ESP_OK;
}
esp_err_t esp_wifi_connect() { assert(wifi_started); ++connect_calls; return ESP_OK; }
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t* config, bool block) {
    assert(wifi_started && !block && std::strcmp(reinterpret_cast<const char*>(config->ssid), "test-ap") == 0);
    return ESP_OK;
}
esp_err_t esp_wifi_scan_get_ap_num(uint16_t* count) { *count = 1; return ESP_OK; }
esp_err_t esp_wifi_scan_get_ap_records(uint16_t*, wifi_ap_record_t* record) {
    *record = {};
    std::memcpy(record->ssid, "test-ap", 7);
    record->rssi = -42;
    return ESP_OK;
}
esp_err_t esp_wifi_clear_ap_list() { return ESP_OK; }
err_t tcpip_try_callback(tcpip_callback_fn callback, void* context) {
    if (fail_enqueue) return ERR_MEM;
    std::lock_guard<std::mutex> lock(dns_mutex);
    tcp_calls.push_back({callback, context});
    return ERR_OK;
}
err_t dns_gethostbyname_addrtype(const char* host, ip_addr_t* address,
                                dns_found_callback callback, void* context, uint8_t) {
    assert(std::strcmp(host, "zectrix.com") == 0);
    ++lookups;
    if (dns_result == ERR_OK) *address = resolved_address;
    if (dns_result == ERR_INPROGRESS) {
        std::lock_guard<std::mutex> lock(dns_mutex);
        dns_calls.push_back({callback, context});
    }
    return dns_result;
}
const char* ipaddr_ntoa_r(const ip_addr_t* address, char* output, int capacity) {
    const auto value = address->addr;
    std::snprintf(output, capacity, "%u.%u.%u.%u", value >> 24, (value >> 16) & 255,
                  (value >> 8) & 255, value & 255);
    return output;
}
esp_err_t esp_crt_bundle_attach(void*) { return ESP_OK; }
esp_tls_t* esp_tls_init() { ++tls_objects; return new esp_tls_t; }
void esp_tls_conn_destroy(esp_tls_t* tls) {
    assert(!http_active);
    cleanup.emplace_back("tls");
    --tls_objects;
    delete tls;
}
int esp_tls_conn_read(esp_tls_t*, void*, std::size_t) { return ESP_TLS_ERR_SSL_WANT_READ; }
int esp_tls_conn_write(esp_tls_t*, const void*, std::size_t) { return ESP_TLS_ERR_SSL_WANT_WRITE; }
esp_err_t esp_tls_get_conn_sockfd(esp_tls_t*, int*) { return ESP_FAIL; }
esp_err_t esp_tls_get_conn_state(esp_tls_t*, esp_tls_conn_state_t* state) { *state = ESP_TLS_INIT; return ESP_OK; }
int esp_tls_conn_new_async(const char* host, int length, int port, const esp_tls_cfg_t* config, esp_tls_t*) {
    assert(std::strcmp(host, "192.0.2.7") == 0 && length == 9 && port == 443);
    assert(std::strcmp(config->common_name, "zectrix.com") == 0 && config->crt_bundle_attach != nullptr);
    return 1;
}

// HTTP framing and the borrowed TLS stream have a separate production test.
struct WifiHttpClient::Impl { uint8_t* body; };
WifiHttpClient::WifiHttpClient() = default;
WifiHttpClient::~WifiHttpClient() { Close(); }
bool WifiHttpClient::Begin(WifiHttpStream&, uint8_t* body, std::size_t capacity) {
    assert(wifi_started && tls_objects == 1 && !http_active && capacity >= 5);
    impl_ = new Impl{body};
    http_active = true;
    return true;
}
WifiDriverResult WifiHttpClient::Poll(std::size_t* size) {
    assert(impl_ != nullptr && http_active);
    *size = 0;
    if (http_result == WifiDriverResult::kReady) {
        std::memcpy(impl_->body, "hello", 5);
        *size = 5;
    }
    return http_result;
}
void WifiHttpClient::Close() {
    if (impl_ == nullptr) return;
    assert(tls_objects == 1);
    http_active = false;
    cleanup.emplace_back("http");
    delete impl_;
    impl_ = nullptr;
}

int main() {
    TestExclusiveClaimAndStartupFailure();
    TestConcurrentEventsAndTeardown();
    TestDnsCancellationAndReuse();
    TestDnsFailuresAndCachedAnswer();
    TestBurstPowersDownBeforeResult();
    TestFailedCleanupRetainsOwnership();
    Reset();
}
