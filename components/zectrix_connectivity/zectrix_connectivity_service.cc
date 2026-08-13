#include "zectrix_connectivity_service.h"

#include <new>

#include "zectrix_ble_link.h"

namespace zectrix::connectivity {
namespace {

ConnectivityResult Map(companion::LinkResult result) {
    switch (result) {
        case companion::LinkResult::kOk: return ConnectivityResult::kOk;
        case companion::LinkResult::kBusy: return ConnectivityResult::kBusy;
        case companion::LinkResult::kUnavailable:
            return ConnectivityResult::kUnavailable;
        case companion::LinkResult::kInvalidArgument:
            return ConnectivityResult::kInvalidState;
        default: return ConnectivityResult::kTransportError;
    }
}

}  // namespace

struct ConnectivityService::Impl {
    BleLink ble;
    bool initialized = false;
};

ConnectivityResult ConnectivityService::Create(ConnectivityService** output) {
    if (output == nullptr) return ConnectivityResult::kInvalidState;
    *output = nullptr;
    auto* impl = new (std::nothrow) Impl();
    if (impl == nullptr) return ConnectivityResult::kUnavailable;
    auto* service = new (std::nothrow) ConnectivityService(impl);
    if (service == nullptr) {
        delete impl;
        return ConnectivityResult::kUnavailable;
    }
    *output = service;
    return ConnectivityResult::kOk;
}

ConnectivityService::~ConnectivityService() { delete impl_; }

ConnectivityResult ConnectivityService::Initialize() {
    if (impl_ == nullptr) return ConnectivityResult::kInvalidState;
    if (impl_->initialized) return ConnectivityResult::kOk;
    const ConnectivityResult result = Map(impl_->ble.Initialize());
    if (result == ConnectivityResult::kOk) impl_->initialized = true;
    return result;
}

ConnectivityResult ConnectivityService::StartLocalPairing() {
    if (impl_ == nullptr || !impl_->initialized) {
        return ConnectivityResult::kInvalidState;
    }
    return Map(impl_->ble.Start());
}

ConnectivityResult ConnectivityService::ClearPeerBonds() {
    if (impl_ == nullptr || !impl_->initialized) {
        return ConnectivityResult::kInvalidState;
    }
    return Map(impl_->ble.ClearBonds());
}

ConnectivityState ConnectivityService::State() const {
    if (impl_ == nullptr) return ConnectivityState::kStopped;
    switch (impl_->ble.State()) {
        case BleState::kIdle: return ConnectivityState::kIdle;
        case BleState::kAdvertising: return ConnectivityState::kAdvertising;
        case BleState::kPairing: return ConnectivityState::kPairing;
        case BleState::kConnectedUnsecured: return ConnectivityState::kSecuring;
        case BleState::kConnectedSecured: return ConnectivityState::kSecure;
        case BleState::kReady: return ConnectivityState::kReady;
        case BleState::kFault: return ConnectivityState::kFault;
        default: return ConnectivityState::kStopped;
    }
}

bool ConnectivityService::TakePairingPasskey(uint32_t* passkey) {
    return impl_ != nullptr && impl_->ble.TakePairingPasskey(passkey);
}

}  // namespace zectrix::connectivity
