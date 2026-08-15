#pragma once

#include <cstdint>

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
    bool peer_authorized = false;
};

class ConnectivityService {
public:
    static ConnectivityResult Create(ConnectivityService** output);
    ~ConnectivityService();

    ConnectivityService(const ConnectivityService&) = delete;
    ConnectivityService& operator=(const ConnectivityService&) = delete;

    ConnectivityResult Initialize();
    ConnectivityResult StartLocalPairing();
    ConnectivityResult ClearPeerBonds();
    ConnectivityState State() const;
    ConnectivitySnapshot Snapshot() const;
    bool TakePairingPasskey(uint32_t* passkey);

private:
    struct Impl;
    explicit ConnectivityService(Impl* impl) : impl_(impl) {}
    Impl* impl_ = nullptr;
};

}  // namespace zectrix::connectivity
