#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "zectrix_resource_client.h"

namespace zectrix::nfc { class NfcService; }
namespace zectrix::storage { class StorageService; }
namespace zectrix::power { struct PowerSnapshot; }

namespace zectrix::connectivity {

enum class ConnectivityState : uint8_t {
    kStopped = 0,
    kIdle,
    kAdvertising,
    kPairing,
    kSecuring,
    kSecure,
    kLinkReady,
    kProtocolNegotiatedLocal,
    kFault,
};

enum class ConnectivityResult : uint8_t {
    kOk = 0,
    kUnavailable,
    kBusy,
    kInvalidState,
    kTransportError,
};

// Product-level diagnostic state. It contains no GAP/GATT handles or stack
// types, and transport readiness does not imply protocol-session readiness.
struct ConnectivitySnapshot {
    ConnectivityState state = ConnectivityState::kStopped;
    uint32_t session_id = 0;
    bool local_pairing_active = false;
    bool encrypted = false;
    bool authenticated = false;
    bool bonded = false;
    bool notifications_enabled = false;
    // The firmware accepted Hello and started HelloAck transport. This does not prove
    // that Android received the response, and it does not authorize the peer.
    bool protocol_negotiated_local = false;
    // Protocol peer authorization is a separate gate. Hello does not set it.
    // A successful NFC-assisted enrollment proof consumes the bootstrap token
    // and sets this flag for the current product session.
    bool peer_authorized = false;
    bool wifi_credentials_available = false;
    bool resource_busy = false;
    WifiBackendState wifi_state = WifiBackendState::kStopped;
    companion::ConnectivityDecision resource_decision{};
};

class ConnectivityService {
public:
    static ConnectivityResult Create(ConnectivityService** output);
    ~ConnectivityService();

    ConnectivityService(const ConnectivityService&) = delete;
    ConnectivityService& operator=(const ConnectivityService&) = delete;

    // Optional dependencies. Call before Initialize().
    void SetNfcService(nfc::NfcService* nfc_service);
    void SetStorageService(storage::StorageService* storage_service);

    ConnectivityResult Initialize();
    ConnectivityResult Stop();
    ConnectivityResult StartLocalPairing();
    ConnectivityResult ClearPeerBonds();
    ConnectivityState State() const;
    ConnectivitySnapshot Snapshot() const;
    bool TakePairingPasskey(uint32_t* passkey);
    // Publish a copied power sample from the application owner before a
    // request. Radio policy treats an invalid battery sample conservatively.
    void UpdatePower(const power::PowerSnapshot& power,
        companion::ProductPowerState state = companion::ProductPowerState::kActive);
    ConnectivityResult SetUserPolicy(companion::UserConnectivityPolicy policy);
    // Internal provisioning API. The caller owns local user authorization.
    ConnectivityResult ConfigureWifi(const WifiCredentials& credentials);
    ConnectivityResult RequestResource(
        const companion::ResourceRequestMessage& request);
    bool TakeResourceResponse(ResourceResponse* response);

private:
    struct Impl;
    explicit ConnectivityService(Impl* impl) : impl_(impl) {}
    Impl* impl_ = nullptr;
};

}  // namespace zectrix::connectivity
