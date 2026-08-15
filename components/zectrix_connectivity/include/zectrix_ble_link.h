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
    kTransportReady,
    kFault,
};

struct ReceivedFrame {
    const uint8_t* data = nullptr;
    std::size_t size = 0;
    uint32_t session_id = 0;
};

struct BleSnapshot {
    BleState state = BleState::kStopped;
    uint32_t session_id = 0;
    bool local_pairing_active = false;
    bool encrypted = false;
    bool authenticated = false;
    bool bonded = false;
    bool notifications_enabled = false;
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
    // Opens a time-bounded pairing window with the supplied duration.
    companion::LinkResult Start(uint32_t pairing_window_ms);
    companion::LinkResult Send(const uint8_t* frame,
                               std::size_t frame_size) override;
    companion::LinkResult SendForSession(uint32_t expected_session_id,
                                         const uint8_t* frame,
                                         std::size_t frame_size);
    void Stop() override;
    companion::LinkStatus Status() const override;

    BleState State() const;
    BleSnapshot Snapshot() const;
    bool TakePairingPasskey(uint32_t* passkey);
    // Wait for a link-state change or a complete received frame. Events can
    // coalesce; callers must inspect the current state and drain available
    // frames after each wake.
    bool WaitForSessionEvent(uint32_t timeout_ms);
    void WakeSessionWaiter();
    bool TakeReceivedFrame(ReceivedFrame* frame);
    void ReleaseReceivedFrame();
    companion::LinkResult ClearBonds();

private:
    Impl* impl_ = nullptr;
};

}  // namespace zectrix::connectivity
