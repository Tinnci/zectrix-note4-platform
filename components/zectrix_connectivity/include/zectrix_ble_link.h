#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "zectrix_connectivity_policy.h"

namespace zectrix::connectivity {

enum class BleState : uint8_t {
    kStopped = 0,
    kIdle,
    kAdvertising,
    kPairing,
    kConnectedUnsecured,
    kConnectedSecured,
    kReady,
    kFault,
};

struct ReceivedFrame {
    const uint8_t* data = nullptr;
    std::size_t size = 0;
};

class BleLink final : public companion::ConnectivityLink {
public:
    // The implementation type is declared for the C callback bridge only.
    // Its definition and all NimBLE types remain private to the .cc file.
    struct Impl;

    BleLink();
    ~BleLink() override;

    BleLink(const BleLink&) = delete;
    BleLink& operator=(const BleLink&) = delete;

    companion::LinkResult Initialize();

    // Initialize starts slow, non-pairable advertising so an existing bonded
    // peer can reconnect. Start temporarily replaces it with a time-bounded,
    // locally authorized pairing window for one new peer.
    companion::LinkResult Start() override;
    companion::LinkResult Send(const uint8_t* frame,
                               std::size_t frame_size) override;
    void Stop() override;
    companion::LinkStatus Status() const override;

    BleState State() const;
    bool TakePairingPasskey(uint32_t* passkey);
    bool TakeReceivedFrame(ReceivedFrame* frame);
    void ReleaseReceivedFrame();
    companion::LinkResult ClearBonds();

private:
    Impl* impl_ = nullptr;
};

}  // namespace zectrix::connectivity
