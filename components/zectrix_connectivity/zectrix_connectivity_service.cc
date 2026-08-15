#include "zectrix_connectivity_service.h"

#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <new>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "zectrix_ble_link.h"
#include "zectrix_companion_protocol.h"
#include "zectrix_nfc_service.h"
#include "zectrix_pairing_bootstrap.h"
#include "zectrix_storage_service.h"

namespace zectrix::connectivity {
namespace {

constexpr char kTag[] = "connectivity";
constexpr uint32_t kSessionPollMs = 250;
constexpr char kCompanionIdentityKey[] = "comp_identity";
constexpr uint32_t kCompanionIdentityMagic = 0x3150435aU;  // "ZCP1" LE
constexpr uint16_t kCompanionIdentityVersion = 1;
constexpr std::size_t kCompanionIdentitySize = 24;

struct CompanionIdentityRecord {
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t reserved = 0;
    uint8_t peer_id[16] = {};
    uint32_t enrollment_generation = 0;
};

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

class EspBootstrapClock final : public companion::PairingBootstrapClock {
public:
    uint32_t MonotonicMilliseconds() const override {
        return static_cast<uint32_t>(esp_timer_get_time() / 1000);
    }
};

class EspBootstrapRandom final : public companion::PairingBootstrapRandom {
public:
    bool Fill(companion::BootstrapToken* token) override {
        if (token == nullptr) return false;
        for (auto& value : *token) {
            value = static_cast<uint8_t>(esp_random() & 0xffU);
        }
        return true;
    }
};

void FillRandomBytes(uint8_t* output, std::size_t size) {
    if (output == nullptr) return;
    for (std::size_t index = 0; index < size; ++index) {
        output[index] = static_cast<uint8_t>(esp_random() & 0xffU);
    }
}

void LogEnrollmentRejection(const char* reason, uint32_t session_id) {
    ESP_LOGW(kTag, "event=enrollment_proof_rejected session=%lu reason=%s",
             static_cast<unsigned long>(session_id), reason);
}

}  // namespace

struct ConnectivityService::Impl {
    BleLink ble;
    nfc::NfcService* nfc_service = nullptr;
    storage::StorageService* storage_service = nullptr;
    EspBootstrapClock bootstrap_clock;
    EspBootstrapRandom bootstrap_random;
    std::unique_ptr<companion::PairingBootstrap> bootstrap;
    bool initialized = false;
    std::atomic<bool> stop_session_task{false};
    std::atomic<bool> protocol_negotiated_local{false};
    std::atomic<bool> peer_authorized{false};
    std::atomic<uint32_t> protocol_session_id{0};
    SemaphoreHandle_t session_task_done = nullptr;

    bool PrepareNfcEnrollment() {
        if (nfc_service == nullptr || bootstrap == nullptr) return false;
        const nfc::NfcSnapshot nfc = nfc_service->Snapshot();
        if (nfc.field_present) return false;
        if (bootstrap->Prepare() != companion::BootstrapStatus::kOk) {
            return false;
        }
        companion::BootstrapMaterial material{};
        if (bootstrap->Material(&material) != companion::BootstrapStatus::kOk) {
            return false;
        }
        const esp_err_t err =
            nfc_service->PrepareEnrollmentNdef(material.generation,
                                               material.token);
        if (err != ESP_OK) {
            ESP_LOGW(kTag,
                     "event=nfc_enrollment_prepare_failed reason=%s",
                     esp_err_to_name(err));
            return false;
        }
        ESP_LOGI(kTag,
                 "event=nfc_enrollment_prepared generation=%lu",
                 static_cast<unsigned long>(material.generation));
        return true;
    }

    void MaybeRefreshNfcEnrollment() {
        if (nfc_service == nullptr || bootstrap == nullptr) return;
        const nfc::NfcSnapshot nfc = nfc_service->Snapshot();
        if (nfc.field_present) return;
        if (bootstrap->state() == companion::BootstrapState::kIdle) {
            PrepareNfcEnrollment();
            return;
        }
        companion::BootstrapMaterial material{};
        if (bootstrap->Material(&material) ==
            companion::BootstrapStatus::kExpired) {
            PrepareNfcEnrollment();
        }
    }

    bool PollNfcFieldAndOpenPairing() {
        if (nfc_service == nullptr || bootstrap == nullptr) return false;
        nfc::NfcFieldEvent event{};
        if (!nfc_service->TakeFieldEvent(&event)) return false;
        if (event != nfc::NfcFieldEvent::kRising) return true;
        const companion::BootstrapStatus status = bootstrap->OpenPairingWindow();
        if (status != companion::BootstrapStatus::kOk) {
            ESP_LOGW(kTag,
                     "event=nfc_pairing_window_rejected reason=%d",
                     static_cast<int>(status));
            return true;
        }
        const companion::LinkResult start = ble.Start();
        ESP_LOGI(kTag,
                 "event=nfc_pairing_window_opened start_result=%d",
                 static_cast<int>(start));
        return true;
    }

    bool ProcessEnrollmentProof(const BleSnapshot& link,
                                const companion::FrameView& frame) {
        if (bootstrap == nullptr) return false;
        companion::TlvReader reader(frame.payload, frame.payload_size);
        companion::TlvField field{};
        bool present = false;
        bool proof_seen = false;
        while (reader.Next(&field, &present) == companion::ProtocolStatus::kOk &&
               present) {
            if (field.type != companion::kHelloEnrollmentProofType) continue;
            proof_seen = true;
            uint32_t generation = 0;
            uint8_t token[16] = {};
            if (companion::DecodeEnrollmentProofValue(
                    field.value, field.value_size, &generation, token) !=
                companion::ProtocolStatus::kOk) {
                LogEnrollmentRejection("malformed_proof", link.session_id);
                return false;
            }
            if (bootstrap->BindSession(link.session_id) !=
                companion::BootstrapStatus::kOk) {
                LogEnrollmentRejection("session_bind_failed", link.session_id);
                return false;
            }
            const companion::BootstrapStatus status =
                bootstrap->ValidateEnrollmentProof(link.session_id, generation,
                                                   token, sizeof(token));
            if (status != companion::BootstrapStatus::kOk) {
                LogEnrollmentRejection("invalid_proof", link.session_id);
                return false;
            }
            peer_authorized.store(true, std::memory_order_release);
            PersistCompanionIdentity(generation);
            ESP_LOGI(kTag,
                     "event=companion_enrolled session=%lu generation=%lu",
                     static_cast<unsigned long>(link.session_id),
                     static_cast<unsigned long>(generation));
            return true;
        }
        if (!proof_seen) {
            ESP_LOGI(kTag,
                     "event=hello_received session=%lu proof=absent",
                     static_cast<unsigned long>(link.session_id));
        }
        return false;
    }

    void PersistCompanionIdentity(uint32_t generation) {
        if (storage_service == nullptr || !storage_service->IsInitialized()) {
            ESP_LOGW(kTag, "event=companion_identity_persist_skipped");
            return;
        }
        CompanionIdentityRecord record{};
        record.magic = kCompanionIdentityMagic;
        record.version = kCompanionIdentityVersion;
        FillRandomBytes(record.peer_id, sizeof(record.peer_id));
        record.enrollment_generation = generation;
        const esp_err_t err = storage_service->SetBlob(
            kCompanionIdentityKey, &record, sizeof(record));
        if (err != ESP_OK) {
            ESP_LOGW(kTag,
                     "event=companion_identity_persist_failed reason=%s",
                     esp_err_to_name(err));
        }
    }

    void LoadCompanionIdentity() {
        if (storage_service == nullptr || !storage_service->IsInitialized()) {
            return;
        }
        CompanionIdentityRecord record{};
        std::size_t length = sizeof(record);
        const esp_err_t err = storage_service->GetBlob(
            kCompanionIdentityKey, &record, &length);
        if (err != ESP_OK) return;
        if (length != sizeof(record) ||
            record.magic != kCompanionIdentityMagic ||
            record.version != kCompanionIdentityVersion ||
            record.reserved != 0) {
            ESP_LOGW(kTag, "event=companion_identity_invalid");
            return;
        }
        peer_authorized.store(true, std::memory_order_release);
        ESP_LOGI(kTag,
                 "event=companion_identity_loaded generation=%lu",
                 static_cast<unsigned long>(record.enrollment_generation));
    }

    static void SessionTask(void* argument) {
        auto* self = static_cast<Impl*>(argument);
        while (!self->stop_session_task.load()) {
            self->MaybeRefreshNfcEnrollment();
            self->PollNfcFieldAndOpenPairing();

            const BleSnapshot link = self->ble.Snapshot();
            if (link.session_id != self->protocol_session_id.load()) {
                self->protocol_session_id.store(link.session_id);
                self->protocol_negotiated_local.store(false);
            }
            if (link.state != BleState::kTransportReady) {
                self->protocol_negotiated_local.store(false);
                self->ble.WaitForSessionEvent(kSessionPollMs);
                continue;
            }
            if (self->protocol_negotiated_local.load()) {
                // The session layer owns only Hello in this slice. Leave all
                // later frames queued for the next protocol consumer.
                self->ble.WaitForSessionEvent(kSessionPollMs);
                continue;
            }
            ReceivedFrame received{};
            if (!self->ble.TakeReceivedFrame(&received)) {
                self->ble.WaitForSessionEvent(kSessionPollMs);
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
            if (frame.payload_size != 0) {
                self->ProcessEnrollmentProof(link, frame);
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

void ConnectivityService::SetNfcService(nfc::NfcService* nfc_service) {
    if (impl_ != nullptr) impl_->nfc_service = nfc_service;
}

void ConnectivityService::SetStorageService(
    storage::StorageService* storage_service) {
    if (impl_ != nullptr) impl_->storage_service = storage_service;
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
    if (result != ConnectivityResult::kOk) return result;

    impl_->LoadCompanionIdentity();
    if (impl_->nfc_service != nullptr) {
        impl_->bootstrap.reset(new (std::nothrow)
            companion::PairingBootstrap(impl_->bootstrap_clock,
                                        impl_->bootstrap_random));
        if (impl_->bootstrap != nullptr) {
            impl_->PrepareNfcEnrollment();
        } else {
            ESP_LOGW(kTag, "event=bootstrap_allocation_failed");
        }
    }
    return ConnectivityResult::kOk;
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
    const ConnectivityResult result = Map(impl_->ble.ClearBonds());
    if (result == ConnectivityResult::kOk) {
        impl_->peer_authorized.store(false, std::memory_order_release);
        if (impl_->storage_service != nullptr &&
            impl_->storage_service->IsInitialized()) {
            const esp_err_t erased =
                impl_->storage_service->Erase(kCompanionIdentityKey);
            if (erased != ESP_OK && erased != ESP_ERR_NOT_FOUND) {
                ESP_LOGW(kTag,
                         "event=companion_identity_erase_failed reason=%s",
                         esp_err_to_name(erased));
            }
        }
        if (impl_->bootstrap != nullptr) {
            impl_->bootstrap->Cancel();
        }
    }
    return result;
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
    snapshot.peer_authorized = impl_->peer_authorized.load();
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
