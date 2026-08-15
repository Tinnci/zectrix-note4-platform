#include "zectrix_connectivity_service.h"

#include <new>
#include <atomic>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "zectrix_ble_link.h"
#include "zectrix_companion_protocol.h"

namespace zectrix::connectivity {
namespace {

constexpr char kTag[] = "connectivity";

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
    std::atomic<bool> stop_session_task{false};
    std::atomic<bool> protocol_negotiated_local{false};
    std::atomic<uint32_t> protocol_session_id{0};
    SemaphoreHandle_t session_task_done = nullptr;

    static void SessionTask(void* argument) {
        auto* self = static_cast<Impl*>(argument);
        while (!self->stop_session_task.load()) {
            const BleSnapshot link = self->ble.Snapshot();
            if (link.session_id != self->protocol_session_id.load()) {
                self->protocol_session_id.store(link.session_id);
                self->protocol_negotiated_local.store(false);
            }
            if (link.state != BleState::kTransportReady) {
                self->protocol_negotiated_local.store(false);
                self->ble.WaitForSessionEvent(UINT32_MAX);
                continue;
            }
            if (self->protocol_negotiated_local.load()) {
                // The session layer owns only Hello in this slice. Leave all
                // later frames queued for the next protocol consumer.
                self->ble.WaitForSessionEvent(UINT32_MAX);
                continue;
            }
            ReceivedFrame received{};
            if (!self->ble.TakeReceivedFrame(&received)) {
                self->ble.WaitForSessionEvent(UINT32_MAX);
                continue;
            }
            companion::FrameView frame{};
            const companion::ProtocolStatus decoded = companion::DecodeFrame(
                received.data, received.size, companion::kProtocolMajor,
                companion::kProtocolMinor, &frame);
            const bool hello = decoded == companion::ProtocolStatus::kOk &&
                frame.header.message_class == companion::MessageClass::kControl &&
                frame.header.message_type ==
                    static_cast<uint16_t>(companion::ControlMessage::kHello) &&
                (frame.header.flags & companion::kResponse) == 0;
            const uint32_t request_id = frame.header.request_id;
            const uint32_t sequence = frame.header.sequence;
            const uint32_t received_session_id = received.session_id;
            self->ble.ReleaseReceivedFrame();
            if (!hello) {
                ESP_LOGW(kTag, "event=protocol_frame_rejected session=%lu reason=expected_hello",
                         static_cast<unsigned long>(link.session_id));
                continue;
            }
            uint8_t response[companion::kFrameHeaderSize]{};
            std::size_t response_size = 0;
            companion::FrameHeader header{};
            header.message_class = companion::MessageClass::kControl;
            header.flags = companion::kResponse;
            header.message_type = static_cast<uint16_t>(
                companion::ControlMessage::kHelloAck);
            header.request_id = request_id;
            header.sequence = sequence;
            if (companion::EncodeFrame(header, nullptr, 0, response,
                                       sizeof(response), &response_size) ==
                    companion::ProtocolStatus::kOk &&
                received_session_id == link.session_id &&
                self->ble.SendForSession(received_session_id, response,
                                         response_size) ==
                    companion::LinkResult::kOk) {
                self->protocol_negotiated_local.store(true);
                ESP_LOGI(kTag,
                         "event=protocol_negotiated_local session=%lu request=%lu",
                         static_cast<unsigned long>(link.session_id),
                         static_cast<unsigned long>(request_id));
            } else {
                ESP_LOGW(kTag, "event=hello_ack_failed session=%lu",
                         static_cast<unsigned long>(link.session_id));
            }
        }
        xSemaphoreGive(self->session_task_done);
        vTaskDelete(nullptr);
    }
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

ConnectivityService::~ConnectivityService() {
    if (impl_ != nullptr) {
        impl_->stop_session_task.store(true);
        impl_->ble.WakeSessionWaiter();
        if (impl_->session_task_done != nullptr) {
            xSemaphoreTake(impl_->session_task_done, portMAX_DELAY);
            vSemaphoreDelete(impl_->session_task_done);
            impl_->session_task_done = nullptr;
        }
    }
    delete impl_;
}

ConnectivityResult ConnectivityService::Initialize() {
    if (impl_ == nullptr) return ConnectivityResult::kInvalidState;
    if (impl_->initialized) return ConnectivityResult::kOk;
    const ConnectivityResult result = Map(impl_->ble.Initialize());
    if (result == ConnectivityResult::kOk) {
        impl_->initialized = true;
        impl_->session_task_done = xSemaphoreCreateBinary();
        if (impl_->session_task_done == nullptr) {
            impl_->ble.Stop();
            impl_->initialized = false;
            return ConnectivityResult::kUnavailable;
        }
        if (xTaskCreate(&Impl::SessionTask, "zectrix_session", 4096, impl_, 4,
                        nullptr) != pdPASS) {
            vSemaphoreDelete(impl_->session_task_done);
            impl_->session_task_done = nullptr;
            impl_->ble.Stop();
            impl_->initialized = false;
            return ConnectivityResult::kUnavailable;
        }
    }
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
    return Snapshot().state;
}

ConnectivitySnapshot ConnectivityService::Snapshot() const {
    ConnectivitySnapshot snapshot{};
    if (impl_ == nullptr) return snapshot;
    const BleSnapshot ble = impl_->ble.Snapshot();
    snapshot.session_id = ble.session_id;
    snapshot.local_pairing_active = ble.local_pairing_active;
    snapshot.encrypted = ble.encrypted;
    snapshot.authenticated = ble.authenticated;
    snapshot.bonded = ble.bonded;
    snapshot.notifications_enabled = ble.notifications_enabled;
    snapshot.protocol_negotiated_local =
        ble.state == BleState::kTransportReady &&
        impl_->protocol_negotiated_local.load() &&
        impl_->protocol_session_id.load() == ble.session_id;
    switch (ble.state) {
        case BleState::kIdle: snapshot.state = ConnectivityState::kIdle; break;
        case BleState::kAdvertising:
            snapshot.state = ConnectivityState::kAdvertising; break;
        case BleState::kPairing: snapshot.state = ConnectivityState::kPairing; break;
        case BleState::kConnectedUnsecured:
            snapshot.state = ConnectivityState::kSecuring; break;
        case BleState::kConnectedSecured:
            snapshot.state = ConnectivityState::kSecure; break;
        case BleState::kTransportReady:
            snapshot.state = snapshot.protocol_negotiated_local
                ? ConnectivityState::kProtocolNegotiatedLocal
                : ConnectivityState::kLinkReady;
            break;
        case BleState::kFault: snapshot.state = ConnectivityState::kFault; break;
        default: snapshot.state = ConnectivityState::kStopped; break;
    }
    return snapshot;
}

bool ConnectivityService::TakePairingPasskey(uint32_t* passkey) {
    return impl_ != nullptr && impl_->ble.TakePairingPasskey(passkey);
}

}  // namespace zectrix::connectivity
