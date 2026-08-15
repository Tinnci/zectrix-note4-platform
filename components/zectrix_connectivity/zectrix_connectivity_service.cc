#include "zectrix_connectivity_service.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <new>

#include "esp_log.h"
#include "esp_mac.h"
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
constexpr char kCompanionIdentityKey[] = "comp_identity";
constexpr uint32_t kCompanionIdentityMagic = 0x3150435aU;  // "ZCP1" LE
constexpr uint16_t kCompanionIdentityVersion = 1;
constexpr std::size_t kCompanionIdentitySize = 24;

constexpr uint16_t kEnrollmentErrorNone = 0;
constexpr uint16_t kEnrollmentErrorMalformedProof = 1;
constexpr uint16_t kEnrollmentErrorSessionBindFailed = 2;
constexpr uint16_t kEnrollmentErrorInvalidProof = 3;
constexpr uint16_t kEnrollmentErrorMissingBootstrap = 4;
constexpr uint16_t kEnrollmentErrorIdentityMismatch = 5;
constexpr uint16_t kEnrollmentErrorNoStoredIdentity = 6;

struct CompanionIdentityRecord {
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t reserved = 0;
    uint8_t companion_id[16] = {};
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

bool IsAllZero(const uint8_t* data, std::size_t size) {
    if (data == nullptr) return true;
    for (std::size_t index = 0; index < size; ++index) {
        if (data[index] != 0) return false;
    }
    return true;
}

bool ConstantTimeEqual(const uint8_t* first, const uint8_t* second,
                       std::size_t size) {
    if (first == nullptr || second == nullptr) return false;
    uint8_t difference = 0;
    for (std::size_t index = 0; index < size; ++index) {
        difference |= static_cast<uint8_t>(first[index] ^ second[index]);
    }
    return difference == 0;
}

void PutUInt16(uint8_t* output, uint16_t value) {
    output[0] = static_cast<uint8_t>(value & 0xffU);
    output[1] = static_cast<uint8_t>((value >> 8U) & 0xffU);
}

void PutUInt32(uint8_t* output, uint32_t value) {
    output[0] = static_cast<uint8_t>(value & 0xffU);
    output[1] = static_cast<uint8_t>((value >> 8U) & 0xffU);
    output[2] = static_cast<uint8_t>((value >> 16U) & 0xffU);
    output[3] = static_cast<uint8_t>((value >> 24U) & 0xffU);
}

uint16_t GetUInt16(const uint8_t* input) {
    return static_cast<uint16_t>(input[0]) |
           static_cast<uint16_t>(input[1]) << 8U;
}

uint32_t GetUInt32(const uint8_t* input) {
    return static_cast<uint32_t>(input[0]) |
           static_cast<uint32_t>(input[1]) << 8U |
           static_cast<uint32_t>(input[2]) << 16U |
           static_cast<uint32_t>(input[3]) << 24U;
}

void EncodeCompanionIdentityRecord(const CompanionIdentityRecord& record,
                                   uint8_t* output) {
    PutUInt32(output, record.magic);
    PutUInt16(output + 4, record.version);
    PutUInt16(output + 6, record.reserved);
    std::memcpy(output + 8, record.companion_id, sizeof(record.companion_id));
    PutUInt32(output + 24 - 4, record.enrollment_generation);
}

bool DecodeCompanionIdentityRecord(const uint8_t* input, std::size_t size,
                                   CompanionIdentityRecord* record) {
    if (input == nullptr || record == nullptr ||
        size != kCompanionIdentitySize) {
        return false;
    }
    record->magic = GetUInt32(input);
    record->version = GetUInt16(input + 4);
    record->reserved = GetUInt16(input + 6);
    std::memcpy(record->companion_id, input + 8,
                sizeof(record->companion_id));
    record->enrollment_generation = GetUInt32(input + 24 - 4);
    return true;
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
    std::atomic<bool> initialized{false};
    std::atomic<bool> stop_session_task{false};
    std::atomic<bool> protocol_negotiated_local{false};
    std::atomic<bool> peer_authorized{false};
    std::atomic<uint32_t> protocol_session_id{0};
    std::atomic<uint32_t> peer_authorized_session_id{0};
    SemaphoreHandle_t session_task_done = nullptr;
    std::array<uint8_t, 16> stored_companion_id{};
    bool stored_companion_id_valid = false;

    bool IsSessionPeerAuthorized(const BleSnapshot& link) const {
        return link.state == BleState::kTransportReady &&
               peer_authorized.load(std::memory_order_acquire) &&
               peer_authorized_session_id.load(std::memory_order_acquire) ==
                   link.session_id;
    }

    void SetSessionPeerAuthorized(uint32_t session_id) {
        peer_authorized_session_id.store(session_id, std::memory_order_release);
        peer_authorized.store(true, std::memory_order_release);
    }

    void ResetSessionPeerAuthorizedIfNeeded(uint32_t session_id) {
        if (peer_authorized_session_id.load(std::memory_order_acquire) !=
            session_id) {
            peer_authorized.store(false, std::memory_order_release);
        }
    }

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

        nfc::EnrollmentNdefInfo info{};
        std::array<uint8_t, 6> mac{};
        if (esp_read_mac(mac.data(), ESP_MAC_BT) == ESP_OK) {
            info.ble_address_type = 0;  // public address
            info.ble_address = mac;
            std::copy(mac.begin(), mac.end(), info.device_id.begin());
        }
        const esp_err_t err = nfc_service->PrepareEnrollmentNdef(
            material.generation, material.token, info);
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "event=nfc_enrollment_prepare_failed reason=%s",
                     esp_err_to_name(err));
            return false;
        }
        ESP_LOGI(kTag, "event=nfc_enrollment_prepared generation=%lu",
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
            ESP_LOGW(kTag, "event=nfc_pairing_window_rejected reason=%d",
                     static_cast<int>(status));
            return true;
        }
        const companion::LinkResult start =
            ble.Start(bootstrap->pairing_window_ms());
        ESP_LOGI(kTag, "event=nfc_pairing_window_opened start_result=%d",
                 static_cast<int>(start));
        return true;
    }

    uint32_t NextSessionWakeMs() const {
        if (bootstrap == nullptr) return UINT32_MAX;
        const uint32_t now = bootstrap_clock.MonotonicMilliseconds();
        const companion::BootstrapState state = bootstrap->state();
        uint32_t deadline = 0;
        if (state == companion::BootstrapState::kPrepared) {
            deadline = bootstrap->token_expires_at_ms();
        } else if (state == companion::BootstrapState::kPairingWindowOpen) {
            deadline = std::min(bootstrap->token_expires_at_ms(),
                                bootstrap->pairing_window_expires_at_ms());
        } else {
            return UINT32_MAX;
        }
        return static_cast<int32_t>(deadline - now) > 0 ? deadline - now : 0;
    }

    struct HelloAckDecision {
        uint8_t status = companion::kHelloAckStatusOk;
        bool peer_authorized = false;
        uint16_t error_reason = kEnrollmentErrorNone;
    };

    HelloAckDecision ProcessHelloPayload(const BleSnapshot& link,
                                         const companion::FrameView& frame) {
        HelloAckDecision decision{};
        if (frame.payload_size == 0) {
            decision.peer_authorized = IsSessionPeerAuthorized(link);
            return decision;
        }

        companion::TlvReader reader(frame.payload, frame.payload_size);
        companion::TlvField field{};
        bool present = false;
        bool enrollment_proof_seen = false;
        bool identity_proof_seen = false;
        while (reader.Next(&field, &present) == companion::ProtocolStatus::kOk &&
               present) {
            if (field.type == companion::kHelloEnrollmentProofType) {
                enrollment_proof_seen = true;
                uint32_t generation = 0;
                uint8_t token[16] = {};
                uint8_t companion_id[16] = {};
                if (companion::DecodeEnrollmentProofValue(
                        field.value, field.value_size, &generation, token,
                        companion_id) != companion::ProtocolStatus::kOk) {
                    LogEnrollmentRejection("malformed_proof", link.session_id);
                    decision.status = companion::kHelloAckStatusRejected;
                    decision.error_reason = kEnrollmentErrorMalformedProof;
                    continue;
                }
                if (IsAllZero(companion_id, sizeof(companion_id))) {
                    LogEnrollmentRejection("missing_identity", link.session_id);
                    decision.status = companion::kHelloAckStatusRejected;
                    decision.error_reason = kEnrollmentErrorMalformedProof;
                    continue;
                }
                if (bootstrap == nullptr) {
                    LogEnrollmentRejection("missing_bootstrap", link.session_id);
                    decision.status = companion::kHelloAckStatusRejected;
                    decision.error_reason = kEnrollmentErrorMissingBootstrap;
                    continue;
                }
                if (bootstrap->BindSession(link.session_id) !=
                    companion::BootstrapStatus::kOk) {
                    LogEnrollmentRejection("session_bind_failed",
                                           link.session_id);
                    decision.status = companion::kHelloAckStatusRejected;
                    decision.error_reason = kEnrollmentErrorSessionBindFailed;
                    continue;
                }
                const companion::BootstrapStatus status =
                    bootstrap->ValidateEnrollmentProof(
                        link.session_id, generation, token, sizeof(token));
                if (status != companion::BootstrapStatus::kOk) {
                    LogEnrollmentRejection("invalid_proof", link.session_id);
                    decision.status = companion::kHelloAckStatusRejected;
                    decision.error_reason = kEnrollmentErrorInvalidProof;
                    continue;
                }
                PersistCompanionIdentity(companion_id, generation);
                SetSessionPeerAuthorized(link.session_id);
                ESP_LOGI(kTag,
                         "event=companion_enrolled session=%lu generation=%lu",
                         static_cast<unsigned long>(link.session_id),
                         static_cast<unsigned long>(generation));
            } else if (field.type == companion::kHelloCompanionIdentityType) {
                identity_proof_seen = true;
                uint8_t companion_id[16] = {};
                if (companion::DecodeCompanionIdentityValue(
                        field.value, field.value_size, companion_id) !=
                    companion::ProtocolStatus::kOk) {
                    LogEnrollmentRejection("malformed_identity",
                                           link.session_id);
                    decision.status = companion::kHelloAckStatusRejected;
                    decision.error_reason = kEnrollmentErrorMalformedProof;
                    continue;
                }
                if (!stored_companion_id_valid) {
                    LogEnrollmentRejection("no_stored_identity",
                                           link.session_id);
                    decision.status = companion::kHelloAckStatusRejected;
                    decision.error_reason = kEnrollmentErrorNoStoredIdentity;
                    continue;
                }
                if (!ConstantTimeEqual(companion_id, stored_companion_id.data(),
                                       stored_companion_id.size())) {
                    LogEnrollmentRejection("identity_mismatch",
                                           link.session_id);
                    decision.status = companion::kHelloAckStatusRejected;
                    decision.error_reason = kEnrollmentErrorIdentityMismatch;
                    continue;
                }
                SetSessionPeerAuthorized(link.session_id);
                ESP_LOGI(kTag,
                         "event=companion_reconnected session=%lu",
                         static_cast<unsigned long>(link.session_id));
            }
        }
        if (!enrollment_proof_seen && !identity_proof_seen) {
            ESP_LOGI(kTag, "event=hello_received session=%lu proof=absent",
                     static_cast<unsigned long>(link.session_id));
        }
        decision.peer_authorized = IsSessionPeerAuthorized(link);
        return decision;
    }

    void PersistCompanionIdentity(const uint8_t companion_id[16],
                                  uint32_t generation) {
        if (storage_service == nullptr || !storage_service->IsInitialized()) {
            ESP_LOGW(kTag, "event=companion_identity_persist_skipped");
            return;
        }
        CompanionIdentityRecord record{};
        record.magic = kCompanionIdentityMagic;
        record.version = kCompanionIdentityVersion;
        std::memcpy(record.companion_id, companion_id,
                    sizeof(record.companion_id));
        record.enrollment_generation = generation;
        std::array<uint8_t, kCompanionIdentitySize> encoded{};
        EncodeCompanionIdentityRecord(record, encoded.data());
        const esp_err_t err = storage_service->SetBlob(
            kCompanionIdentityKey, encoded.data(), encoded.size());
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "event=companion_identity_persist_failed reason=%s",
                     esp_err_to_name(err));
            return;
        }
        std::memcpy(stored_companion_id.data(), companion_id,
                    stored_companion_id.size());
        stored_companion_id_valid = true;
        ESP_LOGI(kTag, "event=companion_identity_persisted generation=%lu",
                 static_cast<unsigned long>(generation));
    }

    void LoadCompanionIdentity() {
        if (storage_service == nullptr || !storage_service->IsInitialized()) {
            return;
        }
        std::array<uint8_t, kCompanionIdentitySize> encoded{};
        std::size_t length = encoded.size();
        const esp_err_t err = storage_service->GetBlob(
            kCompanionIdentityKey, encoded.data(), &length);
        if (err != ESP_OK) return;
        CompanionIdentityRecord record{};
        if (!DecodeCompanionIdentityRecord(encoded.data(), length, &record) ||
            record.magic != kCompanionIdentityMagic ||
            record.version != kCompanionIdentityVersion ||
            record.reserved != 0 ||
            IsAllZero(record.companion_id, sizeof(record.companion_id))) {
            ESP_LOGW(kTag, "event=companion_identity_invalid");
            return;
        }
        std::memcpy(stored_companion_id.data(), record.companion_id,
                    stored_companion_id.size());
        stored_companion_id_valid = true;
        ESP_LOGI(kTag, "event=companion_identity_loaded generation=%lu",
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
            self->ResetSessionPeerAuthorizedIfNeeded(link.session_id);
            const uint32_t session_wake_ms = self->NextSessionWakeMs();

            if (link.state != BleState::kTransportReady) {
                self->protocol_negotiated_local.store(false);
                self->ble.WaitForSessionEvent(session_wake_ms);
                continue;
            }
            if (self->protocol_negotiated_local.load()) {
                // The session layer owns only Hello in this slice. Leave all
                // later frames queued for the next protocol consumer.
                self->ble.WaitForSessionEvent(session_wake_ms);
                continue;
            }
            ReceivedFrame received{};
            if (!self->ble.TakeReceivedFrame(&received)) {
                self->ble.WaitForSessionEvent(session_wake_ms);
                continue;
            }
            companion::FrameView frame{};
            const companion::ProtocolStatus decoded = companion::DecodeFrame(
                received.data, received.size, companion::kProtocolMajor,
                companion::kProtocolMinor, &frame);
            const bool hello =
                decoded == companion::ProtocolStatus::kOk &&
                frame.header.message_class == companion::MessageClass::kControl &&
                frame.header.message_type ==
                    static_cast<uint16_t>(companion::ControlMessage::kHello) &&
                (frame.header.flags & companion::kResponse) == 0;
            const uint32_t request_id = frame.header.request_id;
            const uint32_t sequence = frame.header.sequence;
            const uint32_t received_session_id = received.session_id;
            self->ble.ReleaseReceivedFrame();
            if (!hello) {
                ESP_LOGW(kTag,
                         "event=protocol_frame_rejected session=%lu reason=expected_hello",
                         static_cast<unsigned long>(link.session_id));
                continue;
            }

            const Impl::HelloAckDecision decision =
                self->ProcessHelloPayload(link, frame);

            std::array<uint8_t, 4> status_value{};
            std::size_t status_size = 0;
            companion::EncodeHelloAckStatusValue(
                decision.status,
                decision.peer_authorized
                    ? companion::kHelloAckPeerAuthorizedFlag
                    : 0,
                decision.error_reason, status_value.data(),
                status_value.size(), &status_size);

            std::array<uint8_t, companion::kFrameHeaderSize +
                                   companion::kHelloAckStatusValueSize>
                response{};
            std::size_t response_size = 0;
            companion::FrameHeader header{};
            header.message_class = companion::MessageClass::kControl;
            header.flags = companion::kResponse;
            header.message_type = static_cast<uint16_t>(
                companion::ControlMessage::kHelloAck);
            header.request_id = request_id;
            header.sequence = sequence;
            if (companion::EncodeFrame(header, status_value.data(), status_size,
                                       response.data(), response.size(),
                                       &response_size) ==
                    companion::ProtocolStatus::kOk &&
                received_session_id == link.session_id &&
                self->ble.SendForSession(received_session_id, response.data(),
                                         response_size) ==
                    companion::LinkResult::kOk) {
                self->protocol_negotiated_local.store(true);
                ESP_LOGI(kTag,
                         "event=protocol_negotiated_local session=%lu request=%lu authorized=%d",
                         static_cast<unsigned long>(link.session_id),
                         static_cast<unsigned long>(request_id),
                         decision.peer_authorized ? 1 : 0);
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
        if (impl_->nfc_service != nullptr) {
            impl_->nfc_service->SetEventCallback(nullptr);
        }
        if (impl_->session_task_done != nullptr) {
            xSemaphoreTake(impl_->session_task_done, portMAX_DELAY);
            vSemaphoreDelete(impl_->session_task_done);
            impl_->session_task_done = nullptr;
        }
    }
    delete impl_;
}

void ConnectivityService::SetNfcService(nfc::NfcService* nfc_service) {
    if (impl_ == nullptr || impl_->initialized.load()) return;
    impl_->nfc_service = nfc_service;
}

void ConnectivityService::SetStorageService(
    storage::StorageService* storage_service) {
    if (impl_ == nullptr || impl_->initialized.load()) return;
    impl_->storage_service = storage_service;
}

ConnectivityResult ConnectivityService::Initialize() {
    if (impl_ == nullptr) return ConnectivityResult::kInvalidState;
    if (impl_->initialized.load()) return ConnectivityResult::kOk;

    const ConnectivityResult result = Map(impl_->ble.Initialize());
    if (result != ConnectivityResult::kOk) return result;

    impl_->LoadCompanionIdentity();

    if (impl_->nfc_service != nullptr) {
        impl_->bootstrap.reset(new (std::nothrow)
            companion::PairingBootstrap(impl_->bootstrap_clock,
                                        impl_->bootstrap_random));
        if (impl_->bootstrap != nullptr) {
            impl_->nfc_service->SetEventCallback(
                [impl = impl_]() { impl->ble.WakeSessionWaiter(); });
            impl_->PrepareNfcEnrollment();
        } else {
            ESP_LOGW(kTag, "event=bootstrap_allocation_failed");
        }
    }

    impl_->session_task_done = xSemaphoreCreateBinary();
    if (impl_->session_task_done == nullptr) {
        impl_->ble.Stop();
        if (impl_->nfc_service != nullptr) {
            impl_->nfc_service->SetEventCallback(nullptr);
        }
        return ConnectivityResult::kUnavailable;
    }
    if (xTaskCreate(&Impl::SessionTask, "zectrix_session", 4096, impl_, 4,
                    nullptr) != pdPASS) {
        vSemaphoreDelete(impl_->session_task_done);
        impl_->session_task_done = nullptr;
        impl_->ble.Stop();
        if (impl_->nfc_service != nullptr) {
            impl_->nfc_service->SetEventCallback(nullptr);
        }
        return ConnectivityResult::kUnavailable;
    }

    impl_->initialized.store(true);
    return ConnectivityResult::kOk;
}

ConnectivityResult ConnectivityService::StartLocalPairing() {
    if (impl_ == nullptr || !impl_->initialized.load()) {
        return ConnectivityResult::kInvalidState;
    }
    return Map(impl_->ble.Start());
}

ConnectivityResult ConnectivityService::ClearPeerBonds() {
    if (impl_ == nullptr || !impl_->initialized.load()) {
        return ConnectivityResult::kInvalidState;
    }
    const ConnectivityResult result = Map(impl_->ble.ClearBonds());
    if (result == ConnectivityResult::kOk) {
        impl_->peer_authorized.store(false, std::memory_order_release);
        impl_->peer_authorized_session_id.store(0, std::memory_order_release);
        impl_->stored_companion_id_valid = false;
        impl_->stored_companion_id.fill(0);
        if (impl_->storage_service != nullptr &&
            impl_->storage_service->IsInitialized()) {
            const esp_err_t erased =
                impl_->storage_service->Erase(kCompanionIdentityKey);
            if (erased != ESP_OK && erased != ESP_ERR_NOT_FOUND) {
                ESP_LOGW(kTag, "event=companion_identity_erase_failed reason=%s",
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
    snapshot.peer_authorized =
        ble.state == BleState::kTransportReady &&
        impl_->peer_authorized.load() &&
        impl_->peer_authorized_session_id.load() == ble.session_id;
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
