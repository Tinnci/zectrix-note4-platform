#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "zectrix_resource_gateway.h"

namespace zectrix::nfc { class NfcService; }
namespace zectrix::storage { class StorageService; }

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
};

struct ResourceResponse {
    uint32_t request_id = 0;
    companion::ResourceStatus status =
        companion::ResourceStatus::kInvalidResponse;
    companion::ResourceContentType content_type =
        companion::ResourceContentType::kNone;
    std::array<uint8_t, companion::kResourceMaximumBodySize> body{};
    std::size_t body_size = 0;
    uint32_t retry_after_ms = 0;
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
    ConnectivityResult StartLocalPairing();
    ConnectivityResult ClearPeerBonds();
    ConnectivityState State() const;
    ConnectivitySnapshot Snapshot() const;
    bool TakePairingPasskey(uint32_t* passkey);
    ConnectivityResult RequestResource(
        const companion::ResourceRequestMessage& request);
    bool TakeResourceResponse(ResourceResponse* response);

private:
    struct Impl;
    explicit ConnectivityService(Impl* impl) : impl_(impl) {}
    Impl* impl_ = nullptr;
};

}  // namespace zectrix::connectivity
