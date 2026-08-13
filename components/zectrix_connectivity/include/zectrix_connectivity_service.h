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
    kReady,
    kFault,
};

enum class ConnectivityResult : uint8_t {
    kOk = 0,
    kUnavailable,
    kBusy,
    kInvalidState,
    kTransportError,
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
    bool TakePairingPasskey(uint32_t* passkey);

private:
    struct Impl;
    explicit ConnectivityService(Impl* impl) : impl_(impl) {}
    Impl* impl_ = nullptr;
};

}  // namespace zectrix::connectivity
