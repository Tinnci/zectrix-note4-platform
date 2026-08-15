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
#include "zectrix_companion_identity.h"
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

constexpr uint16_t kEnrollmentErrorNone = 0;
constexpr uint16_t kEnrollmentErrorMalformedProof = 1;
constexpr uint16_t kEnrollmentErrorSessionBindFailed = 2;
constexpr uint16_t kEnrollmentErrorInvalidProof = 3;
constexpr uint16_t kEnrollmentErrorMissingBootstrap = 4;
constexpr uint16_t kEnrollmentErrorIdentityMismatch = 5;
constexpr uint16_t kEnrollmentErrorNoStoredIdentity = 6;
constexpr uint16_t kEnrollmentErrorUnknownRequiredField = 7;
constexpr uint16_t kEnrollmentErrorDuplicateField = 8;

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
    SemaphoreHandle_t clear_bonds_mutex = nullptr;
    SemaphoreHandle_t clear_bonds_done = nullptr;
    std::atomic<bool> clear_bonds_requested{false};
    std::atomic<ConnectivityResult> clear_bonds_result{
        ConnectivityResult::kTransportError};
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

    // Runs in the session task only. ClearPeerBonds() submits the request
    // through clear_bonds_requested and blocks on clear_bonds_done so that
    // bootstrap/identity state is only ever mutated by the session owner.
    void ProcessPendingCommands() {
        if (!clear_bonds_requested.exchange(false, std::memory_order_acquire)) {
            return;
        }
        const ConnectivityResult result = Map(ble.ClearBonds());
        if (result == ConnectivityResult::kOk) {
            peer_authorized.store(false, std::memory_order_release);
            peer_authorized_session_id.store(0, std::memory_order_release);
            stored_companion_id_valid = false;
            stored_companion_id.fill(0);
            if (storage_service != nullptr &&
                storage_service->IsInitialized()) {
                const esp_err_t erased =
                    storage_service->Erase(kCompanionIdentityKey);
                if (erased != ESP_OK && erased != ESP_ERR_NOT_FOUND) {
                    ESP_LOGW(kTag,
                             "event=companion_identity_erase_failed reason=%s",
                             esp_err_to_name(erased));
                }
            }
            if (bootstrap != nullptr) bootstrap->Cancel();
        }
        clear_bonds_result.store(result, std::memory_order_release);
        xSemaphoreGive(clear_bonds_done);
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
        if (bootstrap->state() == companion::BootstrapState::kIdle ||
            bootstrap->state() == companion::BootstrapState::kExpired) {
            bootstrap->Cancel();
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

    HelloAckDecision ProcessHelloPayload(uint32_t session_id,
                                         const BleSnapshot& link,
                                         const companion::FrameView& frame) {
        HelloAckDecision decision{};
        if (frame.payload_size == 0) {
            decision.peer_authorized = IsSessionPeerAuthorized(link);
            return decision;
        }

        bool has_proof = false;
        bool has_identity = false;
        uint32_t proof_generation = 0;
        uint8_t proof_token[16] = {};
        uint8_t proof_companion_id[16] = {};
        uint8_t identity_companion_id[16] = {};

        companion::TlvReader reader(frame.payload, frame.payload_size);
        companion::TlvField field{};
        bool present = false;
        while (true) {
            const companion::ProtocolStatus status = reader.Next(&field, &present);
            if (status != companion::ProtocolStatus::kOk) {
                LogEnrollmentRejection("malformed_tlv", session_id);
                decision.status = companion::kHelloAckStatusRejected;
                decision.error_reason = kEnrollmentErrorMalformedProof;
                return decision;
            }
            if (!present) break;
            if (field.type == companion::kHelloEnrollmentProofType) {
                if (has_proof) {
                    LogEnrollmentRejection("duplicate_proof", session_id);
                    decision.status = companion::kHelloAckStatusRejected;
                    decision.error_reason = kEnrollmentErrorDuplicateField;
                    return decision;
                }
                has_proof = true;
                if (companion::DecodeEnrollmentProofValue(
                        field.value, field.value_size, &proof_generation,
                        proof_token, proof_companion_id) !=
                        companion::ProtocolStatus::kOk ||
                    IsAllZero(proof_companion_id, sizeof(proof_companion_id))) {
                    LogEnrollmentRejection("malformed_proof", session_id);
                    decision.status = companion::kHelloAckStatusRejected;
                    decision.error_reason = kEnrollmentErrorMalformedProof;
                    return decision;
                }
            } else if (field.type == companion::kHelloCompanionIdentityType) {
                if (has_identity) {
                    LogEnrollmentRejection("duplicate_identity", session_id);
                    decision.status = companion::kHelloAckStatusRejected;
                    decision.error_reason = kEnrollmentErrorDuplicateField;
                    return decision;
                }
                has_identity = true;
                if (companion::DecodeCompanionIdentityValue(
                        field.value, field.value_size,
                        identity_companion_id) !=
                        companion::ProtocolStatus::kOk ||
                    IsAllZero(identity_companion_id,
                              sizeof(identity_companion_id))) {
                    LogEnrollmentRejection("malformed_identity", session_id);
                    decision.status = companion::kHelloAckStatusRejected;
                    decision.error_reason = kEnrollmentErrorMalformedProof;
                    return decision;
                }
            } else if (field.required) {
                LogEnrollmentRejection("unknown_required_field", session_id);
                decision.status = companion::kHelloAckStatusRejected;
                decision.error_reason = kEnrollmentErrorUnknownRequiredField;
                return decision;
            }
        }

        if (!has_proof && !has_identity) {
            ESP_LOGI(kTag, "event=hello_received session=%lu proof=absent",
                     static_cast<unsigned long>(session_id));
            decision.peer_authorized = IsSessionPeerAuthorized(link);
            return decision;
        }

        if (has_proof) {
            if (bootstrap == nullptr) {
                LogEnrollmentRejection("missing_bootstrap", session_id);
                peer_authorized.store(false, std::memory_order_release);
                decision.status = companion::kHelloAckStatusRejected;
                decision.error_reason = kEnrollmentErrorMissingBootstrap;
            } else {
                companion::BootstrapStatus status =
                    companion::BootstrapStatus::kInvalidState;
                // Consume the single-use token only while the session is
                // still current and transport-ready. Holding the link lock
                // across validation closes the window between the session
                // check and token consumption.
                const bool session_current = ble.WithCurrentTransportSession(
                    session_id, [&]() {
                        status = bootstrap->ValidateEnrollmentProof(
                            session_id, proof_generation, proof_token,
                            sizeof(proof_token));
                    });
                if (!session_current) {
                    LogEnrollmentRejection("session_changed", session_id);
                    peer_authorized.store(false, std::memory_order_release);
                    decision.status = companion::kHelloAckStatusRejected;
                    decision.error_reason = kEnrollmentErrorSessionBindFailed;
                } else if (status != companion::BootstrapStatus::kOk) {
                    LogEnrollmentRejection("invalid_proof", session_id);
                    peer_authorized.store(false, std::memory_order_release);
                    decision.status = companion::kHelloAckStatusRejected;
                    decision.error_reason = kEnrollmentErrorInvalidProof;
                } else {
                    PersistCompanionIdentity(proof_companion_id,
                                             proof_generation);
                    SetSessionPeerAuthorized(session_id);
                    ESP_LOGI(kTag,
                             "event=companion_enrolled session=%lu generation=%lu",
                             static_cast<unsigned long>(session_id),
                             static_cast<unsigned long>(proof_generation));
                }
            }
        }

        if (decision.status == companion::kHelloAckStatusOk && has_identity) {
            if (!stored_companion_id_valid) {
                LogEnrollmentRejection("no_stored_identity", session_id);
                peer_authorized.store(false, std::memory_order_release);
                decision.status = companion::kHelloAckStatusRejected;
                decision.error_reason = kEnrollmentErrorNoStoredIdentity;
            } else if (!ConstantTimeEqual(identity_companion_id,
                                          stored_companion_id.data(),
                                          stored_companion_id.size())) {
                LogEnrollmentRejection("identity_mismatch", session_id);
                peer_authorized.store(false, std::memory_order_release);
                decision.status = companion::kHelloAckStatusRejected;
                decision.error_reason = kEnrollmentErrorIdentityMismatch;
            } else {
                SetSessionPeerAuthorized(session_id);
                ESP_LOGI(kTag, "event=companion_reconnected session=%lu",
                         static_cast<unsigned long>(session_id));
            }
        }

        if (decision.status == companion::kHelloAckStatusOk) {
            decision.peer_authorized = IsSessionPeerAuthorized(link);
        }
        return decision;
    }

    void PersistCompanionIdentity(const uint8_t companion_id[16],
                                  uint32_t generation) {
        if (storage_service == nullptr || !storage_service->IsInitialized()) {
            ESP_LOGW(kTag, "event=companion_identity_persist_skipped");
            return;
        }
        companion::CompanionIdentityRecord record{};
        record.magic = kCompanionIdentityMagic;
        record.version = kCompanionIdentityVersion;
        std::memcpy(record.companion_id, companion_id,
                    sizeof(record.companion_id));
        record.enrollment_generation = generation;
        std::array<uint8_t, companion::kCompanionIdentityRecordSize> encoded{};
        companion::EncodeCompanionIdentityRecord(record, encoded.data());
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
        std::array<uint8_t, companion::kCompanionIdentityRecordSize> encoded{};
        std::size_t length = encoded.size();
        const esp_err_t err = storage_service->GetBlob(
            kCompanionIdentityKey, encoded.data(), &length);
        if (err != ESP_OK) return;
        companion::CompanionIdentityRecord record{};
        if (!companion::DecodeCompanionIdentityRecord(
                encoded.data(), length, &record) ||
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
            self->ProcessPendingCommands();
            self->ble.ProcessAdvertiseRequest();
            self->MaybeRefreshNfcEnrollment();
            self->PollNfcFieldAndOpenPairing();

            BleSnapshot link = self->ble.Snapshot();
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
            const uint32_t received_session_id = received.session_id;
            // Re-read the link after borrowing the frame. The first snapshot
            // may predate a disconnect/reconnect, and a proof from the old
            // session must not consume the bootstrap token.
            link = self->ble.Snapshot();
            if (received_session_id != link.session_id ||
                link.state != BleState::kTransportReady) {
                self->ble.ReleaseReceivedFrame();
                ESP_LOGW(kTag,
                         "event=stale_frame_discarded session=%lu frame_session=%lu",
                         static_cast<unsigned long>(link.session_id),
                         static_cast<unsigned long>(received_session_id));
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
            if (!hello) {
                self->ble.ReleaseReceivedFrame();
                ESP_LOGW(kTag,
                         "event=protocol_frame_rejected session=%lu reason=expected_hello",
                         static_cast<unsigned long>(link.session_id));
                continue;
            }
            const uint32_t request_id = frame.header.request_id;
            const uint32_t sequence = frame.header.sequence;

            const Impl::HelloAckDecision decision =
                self->ProcessHelloPayload(received_session_id, link, frame);
            self->ble.ReleaseReceivedFrame();

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
        if (impl_->clear_bonds_done != nullptr) {
            vSemaphoreDelete(impl_->clear_bonds_done);
            impl_->clear_bonds_done = nullptr;
        }
        if (impl_->clear_bonds_mutex != nullptr) {
            vSemaphoreDelete(impl_->clear_bonds_mutex);
            impl_->clear_bonds_mutex = nullptr;
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

    impl_->clear_bonds_mutex = xSemaphoreCreateMutex();
    impl_->clear_bonds_done = xSemaphoreCreateBinary();
    if (impl_->clear_bonds_mutex == nullptr || impl_->clear_bonds_done == nullptr) {
        if (impl_->clear_bonds_mutex != nullptr) {
            vSemaphoreDelete(impl_->clear_bonds_mutex);
            impl_->clear_bonds_mutex = nullptr;
        }
        if (impl_->clear_bonds_done != nullptr) {
            vSemaphoreDelete(impl_->clear_bonds_done);
            impl_->clear_bonds_done = nullptr;
        }
        impl_->ble.Stop();
        if (impl_->nfc_service != nullptr) {
            impl_->nfc_service->SetEventCallback(nullptr);
        }
        return ConnectivityResult::kUnavailable;
    }

    impl_->session_task_done = xSemaphoreCreateBinary();
    if (impl_->session_task_done == nullptr) {
        vSemaphoreDelete(impl_->clear_bonds_mutex);
        vSemaphoreDelete(impl_->clear_bonds_done);
        impl_->clear_bonds_mutex = nullptr;
        impl_->clear_bonds_done = nullptr;
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
        vSemaphoreDelete(impl_->clear_bonds_mutex);
        vSemaphoreDelete(impl_->clear_bonds_done);
        impl_->clear_bonds_mutex = nullptr;
        impl_->clear_bonds_done = nullptr;
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
    if (impl_->clear_bonds_mutex == nullptr ||
        impl_->clear_bonds_done == nullptr) {
        return ConnectivityResult::kUnavailable;
    }
    xSemaphoreTake(impl_->clear_bonds_mutex, portMAX_DELAY);
    impl_->clear_bonds_requested.store(true, std::memory_order_release);
    impl_->ble.WakeSessionWaiter();
    xSemaphoreTake(impl_->clear_bonds_done, portMAX_DELAY);
    const ConnectivityResult result =
        impl_->clear_bonds_result.load(std::memory_order_acquire);
    xSemaphoreGive(impl_->clear_bonds_mutex);
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
