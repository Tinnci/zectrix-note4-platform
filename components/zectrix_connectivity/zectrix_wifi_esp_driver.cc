#include "zectrix_wifi_esp_driver.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <ctime>
#include <new>

#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/tcpip.h"
#include "zectrix_wifi_http.h"

namespace zectrix::connectivity {
namespace {

constexpr char kResourceHost[] = "zectrix.com";
constexpr char kResourceGet[] =
    "GET /robots.txt HTTP/1.1\r\n"
    "Host: zectrix.com\r\n"
    "Accept: text/plain\r\n"
    "Accept-Encoding: identity\r\n"
    "Connection: close\r\n"
    "User-Agent: Zectrix-Note4/1\r\n\r\n";

std::atomic<bool> radio_claimed{false};

bool Supported(companion::ResourceCapability capability) {
    return capability == companion::ResourceCapability::kPublicTestDocumentV1;
}

bool TlsPending(int result) {
    return result == ESP_TLS_ERR_SSL_WANT_READ ||
           result == ESP_TLS_ERR_SSL_WANT_WRITE;
}

// lwIP cannot cancel a submitted DNS query. A reference owned by its callback
// keeps this small context alive after a timeout, without retaining a driver.
struct DnsQuery {
    std::atomic<unsigned> references{2};
    std::atomic<bool> cancelled{false};
    std::atomic<WifiDriverResult> result{WifiDriverResult::kPending};
    std::array<char, 16> address{};

    void Release() {
        if (references.fetch_sub(1, std::memory_order_acq_rel) == 1) delete this;
    }

    static void Complete(const char*, const ip_addr_t* address, void* context) {
        auto* query = static_cast<DnsQuery*>(context);
        const bool valid = address != nullptr && IP_IS_V4(address) &&
            !ip_addr_isany(address) &&
            ipaddr_ntoa_r(address, query->address.data(),
                          query->address.size()) != nullptr;
        query->result.store(valid ? WifiDriverResult::kReady
                                  : WifiDriverResult::kDnsFailure,
                            std::memory_order_release);
        query->Release();
    }

    static void Start(void* context) {
        auto* query = static_cast<DnsQuery*>(context);
        if (query->cancelled.load(std::memory_order_acquire)) {
            query->Release();
            return;
        }
        ip_addr_t address{};
        const err_t result = dns_gethostbyname_addrtype(
            kResourceHost, &address, &Complete, query, LWIP_DNS_ADDRTYPE_IPV4);
        if (result != ERR_INPROGRESS) {
            Complete(nullptr, result == ERR_OK ? &address : nullptr, query);
        }
    }
};

WifiDriverResult DisconnectionFailure(uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_802_1X_AUTH_FAILED:
            return WifiDriverResult::kAuthRejected;
        default:
            return WifiDriverResult::kUnavailable;
    }
}

}  // namespace

struct EspWifiBackendDriver::Impl {
    esp_netif_t* netif = nullptr;
    esp_event_handler_instance_t wifi_handler = nullptr;
    esp_event_handler_instance_t ip_handler = nullptr;
    esp_tls_t* tls = nullptr;
    DnsQuery* dns = nullptr;
    WifiHttpResponse http;
    std::atomic<WifiDriverResult> link_error{WifiDriverResult::kPending};
    std::atomic<bool> station_ready{false};
    std::atomic<bool> associated{false};
    std::atomic<bool> has_ip{false};
    std::atomic<bool> scan_done{false};
    std::atomic<bool> stopping{false};
    bool claimed = false;
    bool wifi_initialized = false;
    bool wifi_started = false;
    bool connect_requested = false;
    bool scan_mode = false;
    bool scan_requested = false;
    bool http_started = false;
    std::size_t request_sent = 0;
    std::array<char, kMaximumWifiSsidBytes + 1> scan_target{};

    static void OnEvent(void* context, esp_event_base_t base,
                        int32_t id, void* data) {
        auto* self = static_cast<Impl*>(context);
        if (self->stopping.load(std::memory_order_acquire)) return;
        if (base == WIFI_EVENT) {
            switch (id) {
                case WIFI_EVENT_STA_START:
                    self->station_ready.store(true); break;
                case WIFI_EVENT_STA_CONNECTED:
                    self->associated.store(true); break;
                case WIFI_EVENT_STA_DISCONNECTED: {
                    self->associated.store(false);
                    self->has_ip.store(false);
                    const auto* event =
                        static_cast<wifi_event_sta_disconnected_t*>(data);
                    self->link_error.store(event == nullptr
                        ? WifiDriverResult::kUnavailable
                        : DisconnectionFailure(event->reason));
                    break;
                }
                case WIFI_EVENT_SCAN_DONE:
                    self->scan_done.store(true); break;
                default: break;
            }
        } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
            const auto* event = static_cast<ip_event_got_ip_t*>(data);
            if (event != nullptr && event->esp_netif == self->netif &&
                event->ip_info.ip.addr != 0) self->has_ip.store(true);
        } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
            self->has_ip.store(false);
            self->link_error.store(WifiDriverResult::kIpFailure);
        }
    }

    WifiDriverResult Start(const WifiCredentials* credentials) {
        bool expected = false;
        if (claimed || !radio_claimed.compare_exchange_strong(expected, true)) {
            return WifiDriverResult::kUnavailable;
        }
        claimed = true;
        stopping.store(false);
        station_ready.store(false);
        associated.store(false);
        has_ip.store(false);
        scan_done.store(false);
        link_error.store(WifiDriverResult::kPending);
        scan_mode = credentials == nullptr;
        esp_err_t error = esp_netif_init();
        if (error == ESP_OK || error == ESP_ERR_INVALID_STATE) {
            error = esp_event_loop_create_default();
        }
        if (error == ESP_ERR_INVALID_STATE) error = ESP_OK;
        if (error == ESP_OK) {
            const esp_netif_config_t config = ESP_NETIF_DEFAULT_WIFI_STA();
            netif = esp_netif_new(&config);
            error = netif == nullptr ? ESP_ERR_NO_MEM : ESP_OK;
        }
        if (error == ESP_OK) error = esp_netif_attach_wifi_station(netif);
        if (error == ESP_OK) error = esp_wifi_set_default_wifi_sta_handlers();
        if (error == ESP_OK) {
            wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
            error = esp_wifi_init(&config);
            wifi_initialized = error == ESP_OK;
        }
        if (error == ESP_OK) {
            error = esp_event_handler_instance_register(
                WIFI_EVENT, ESP_EVENT_ANY_ID, &OnEvent, this, &wifi_handler);
        }
        if (error == ESP_OK) {
            error = esp_event_handler_instance_register(
                IP_EVENT, ESP_EVENT_ANY_ID, &OnEvent, this, &ip_handler);
        }
        if (error == ESP_OK) error = esp_wifi_set_storage(WIFI_STORAGE_RAM);
        if (error == ESP_OK) error = esp_wifi_set_mode(WIFI_MODE_STA);
        if (error == ESP_OK && credentials != nullptr) {
            wifi_config_t config{};
            std::memcpy(config.sta.ssid, credentials->ssid.data(),
                        std::strlen(credentials->ssid.data()));
            std::memcpy(config.sta.password, credentials->passphrase.data(),
                        std::strlen(credentials->passphrase.data()));
            config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
            config.sta.threshold.authmode = credentials->passphrase[0] == '\0'
                ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
            config.sta.pmf_cfg.capable = true;
            config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
            error = esp_wifi_set_config(WIFI_IF_STA, &config);
            volatile uint8_t* bytes = reinterpret_cast<volatile uint8_t*>(&config);
            for (std::size_t index = 0; index < sizeof(config); ++index) bytes[index] = 0;
        }
        if (error == ESP_OK) {
            error = esp_wifi_start();
            wifi_started = error == ESP_OK;
        }
        if (error != ESP_OK) link_error.store(WifiDriverResult::kUnavailable);
        // Once the radio is claimed, even a partial start is accepted so that
        // WifiBackend always runs StopStation and reports cleanup separately.
        return WifiDriverResult::kPending;
    }

    WifiDriverResult LinkResult() const {
        return claimed ? link_error.load() : WifiDriverResult::kUnavailable;
    }

    void ReleaseDns() {
        if (dns == nullptr) return;
        dns->cancelled.store(true, std::memory_order_release);
        dns->Release();
        dns = nullptr;
    }

    WifiDriverResult Stop() {
        if (!claimed) return WifiDriverResult::kReady;
        stopping.store(true, std::memory_order_release);
        if (tls != nullptr) {
            esp_tls_conn_destroy(tls);
            tls = nullptr;
        }
        ReleaseDns();
        if (wifi_started) {
            const esp_err_t error = esp_wifi_stop();
            if (error != ESP_OK && error != ESP_ERR_WIFI_NOT_STARTED) {
                return WifiDriverResult::kUnavailable;
            }
            wifi_started = false;
        }
        if (wifi_initialized) {
            if (esp_wifi_deinit() != ESP_OK) return WifiDriverResult::kUnavailable;
            wifi_initialized = false;
        }
        if (wifi_handler != nullptr) {
            esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                   wifi_handler);
            wifi_handler = nullptr;
        }
        if (ip_handler != nullptr) {
            esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID,
                                                   ip_handler);
            ip_handler = nullptr;
        }
        if (netif != nullptr) {
            esp_wifi_clear_default_wifi_driver_and_handlers(netif);
            esp_netif_destroy(netif);
            netif = nullptr;
        }
        connect_requested = false;
        scan_requested = false;
        http_started = false;
        request_sent = 0;
        http = {};
        claimed = false;
        radio_claimed.store(false, std::memory_order_release);
        return WifiDriverResult::kReady;
    }
};

EspWifiBackendDriver::EspWifiBackendDriver() : impl_(new (std::nothrow) Impl()) {}

EspWifiBackendDriver::~EspWifiBackendDriver() {
    if (impl_ == nullptr) return;
    if (impl_->Stop() != WifiDriverResult::kReady) {
        // Keep callback storage and the exclusive claim alive after an IDF
        // stop failure. A new owner must not start a possibly running radio.
        return;
    }
    delete impl_;
}

WifiDriverResult EspWifiBackendDriver::StartStation(
    const WifiCredentials& credentials) {
    if (impl_ == nullptr || !ValidateWifiCredentials(credentials)) {
        return WifiDriverResult::kUnavailable;
    }
    return impl_->Start(&credentials);
}

WifiDriverResult EspWifiBackendDriver::PollAssociation() {
    if (impl_ == nullptr) return WifiDriverResult::kUnavailable;
    const auto result = impl_->LinkResult();
    if (result != WifiDriverResult::kPending) return result;
    if (!impl_->station_ready.load()) return WifiDriverResult::kPending;
    if (!impl_->connect_requested && !impl_->scan_mode) {
        impl_->connect_requested = true;
        if (esp_wifi_connect() != ESP_OK) {
            impl_->link_error.store(WifiDriverResult::kUnavailable);
            return WifiDriverResult::kUnavailable;
        }
    }
    return impl_->associated.load() ? WifiDriverResult::kReady
                                    : WifiDriverResult::kPending;
}

WifiDriverResult EspWifiBackendDriver::PollIp() {
    if (impl_ == nullptr) return WifiDriverResult::kUnavailable;
    const auto result = impl_->LinkResult();
    if (result != WifiDriverResult::kPending) return result;
    return impl_->has_ip.load() ? WifiDriverResult::kReady
                                : WifiDriverResult::kPending;
}

WifiDriverResult EspWifiBackendDriver::Resolve(
    companion::ResourceCapability capability) {
    if (impl_ == nullptr || !Supported(capability)) {
        return WifiDriverResult::kUnavailable;
    }
    const auto link = PollIp();
    if (link != WifiDriverResult::kReady) return link;
    if (impl_->dns == nullptr) {
        impl_->dns = new (std::nothrow) DnsQuery();
        if (impl_->dns == nullptr) return WifiDriverResult::kDnsFailure;
        if (tcpip_try_callback(&DnsQuery::Start, impl_->dns) != ERR_OK) {
            impl_->dns->Release();
            impl_->ReleaseDns();
            return WifiDriverResult::kDnsFailure;
        }
    }
    return impl_->dns->result.load(std::memory_order_acquire);
}

WifiDriverResult EspWifiBackendDriver::OpenTls(
    companion::ResourceCapability capability) {
    if (impl_ == nullptr || !Supported(capability) || impl_->dns == nullptr) {
        return WifiDriverResult::kUnavailable;
    }
    const auto link = PollIp();
    if (link != WifiDriverResult::kReady) return link;
    if (impl_->dns->result.load(std::memory_order_acquire) != WifiDriverResult::kReady) {
        return WifiDriverResult::kDnsFailure;
    }
    // Certificate dates must be checked against a configured wall clock.
    if (std::time(nullptr) < 1704067200) return WifiDriverResult::kTlsFailure;
    if (impl_->tls == nullptr) impl_->tls = esp_tls_init();
    if (impl_->tls == nullptr) return WifiDriverResult::kTlsFailure;
    esp_tls_cfg_t config{};
    config.non_block = true;
    config.timeout_ms = 20;
    config.common_name = kResourceHost;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    const int result = esp_tls_conn_new_async(
        impl_->dns->address.data(), std::strlen(impl_->dns->address.data()),
        443, &config, impl_->tls);
    return result == 1 ? WifiDriverResult::kReady
        : result == 0 ? WifiDriverResult::kPending : WifiDriverResult::kTlsFailure;
}

WifiDriverResult EspWifiBackendDriver::Fetch(
    companion::ResourceCapability capability, uint8_t* body,
    std::size_t capacity, std::size_t* body_size) {
    if (impl_ == nullptr || !Supported(capability) || impl_->tls == nullptr ||
        body_size == nullptr) return WifiDriverResult::kUnavailable;
    *body_size = 0;
    const auto link = PollIp();
    if (link != WifiDriverResult::kReady) return link;
    if (!impl_->http_started) {
        if (!impl_->http.Begin(body, capacity)) return WifiDriverResult::kInvalidResponse;
        impl_->http_started = true;
    }
    if (impl_->request_sent < sizeof(kResourceGet) - 1) {
        const int written = esp_tls_conn_write(
            impl_->tls, kResourceGet + impl_->request_sent,
            sizeof(kResourceGet) - 1 - impl_->request_sent);
        if (TlsPending(written)) return WifiDriverResult::kPending;
        if (written <= 0) return WifiDriverResult::kTransferFailure;
        impl_->request_sent += static_cast<std::size_t>(written);
        return WifiDriverResult::kPending;
    }
    std::array<uint8_t, 512> input{};
    const int received = esp_tls_conn_read(impl_->tls, input.data(), input.size());
    if (TlsPending(received)) return WifiDriverResult::kPending;
    if (received < 0) return WifiDriverResult::kTransferFailure;
    const WifiDriverResult result = received == 0
        ? impl_->http.EndOfStream()
        : impl_->http.Feed(input.data(), static_cast<std::size_t>(received));
    if (result == WifiDriverResult::kReady) *body_size = impl_->http.BodySize();
    return result;
}

WifiDriverResult EspWifiBackendDriver::StopStation() {
    return impl_ == nullptr ? WifiDriverResult::kReady : impl_->Stop();
}

WifiDriverResult EspWifiBackendDriver::StartScan() {
    return impl_ == nullptr ? WifiDriverResult::kUnavailable : impl_->Start(nullptr);
}

WifiDriverResult EspWifiBackendDriver::PollScan(
    const char* target, WifiScanSnapshot* snapshot) {
    if (impl_ == nullptr || snapshot == nullptr || target == nullptr ||
        !impl_->scan_mode || std::strlen(target) > kMaximumWifiSsidBytes) {
        return WifiDriverResult::kUnavailable;
    }
    const auto result = impl_->LinkResult();
    if (result != WifiDriverResult::kPending) return result;
    if (!impl_->station_ready.load()) return WifiDriverResult::kPending;
    if (!impl_->scan_requested) {
        std::strcpy(impl_->scan_target.data(), target);
        wifi_scan_config_t config{};
        config.show_hidden = true;
        config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
        config.ssid = target[0] == '\0' ? nullptr
            : reinterpret_cast<uint8_t*>(impl_->scan_target.data());
        impl_->scan_done.store(false);
        if (esp_wifi_scan_start(&config, false) != ESP_OK) {
            return WifiDriverResult::kUnavailable;
        }
        impl_->scan_requested = true;
        return WifiDriverResult::kPending;
    }
    if (!impl_->scan_done.load()) return WifiDriverResult::kPending;
    impl_->scan_requested = false;
    *snapshot = {};
    uint16_t total = 0;
    if (esp_wifi_scan_get_ap_num(&total) != ESP_OK) {
        esp_wifi_clear_ap_list();
        return WifiDriverResult::kUnavailable;
    }
    snapshot->access_point_count = total;
    // Records are ordered by RSSI; the first record is sufficient for either
    // generic RF detection or an SSID-filtered qualification scan.
    wifi_ap_record_t best{};
    uint16_t count = total == 0 ? 0 : 1;
    const esp_err_t error = count == 0 ? ESP_OK
        : esp_wifi_scan_get_ap_records(&count, &best);
    esp_wifi_clear_ap_list();
    if (error != ESP_OK) return WifiDriverResult::kUnavailable;
    if (count != 0) {
        snapshot->best_rssi = best.rssi;
        std::memcpy(snapshot->best_ssid.data(), best.ssid, kMaximumWifiSsidBytes);
        snapshot->target_found = target[0] != '\0' &&
            std::strcmp(snapshot->best_ssid.data(), target) == 0;
    }
    return WifiDriverResult::kReady;
}

}  // namespace zectrix::connectivity
