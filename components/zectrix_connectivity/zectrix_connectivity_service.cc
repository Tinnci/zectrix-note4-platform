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
#include "zectrix_power_service.h"
#include "zectrix_storage_service.h"
#include "zectrix_sync_session.h"
#include "zectrix_wifi_credentials.h"
#include "zectrix_wifi_esp_driver.h"

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
constexpr uint16_t kEnrollmentErrorStore = 9;
constexpr uint16_t kEnrollmentErrorSyncRequired = 10;
constexpr uint16_t kEnrollmentErrorSyncCursors = 11;
constexpr char kSyncStateKey[] = "comp_sync";

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

struct ConnectivityService::Impl : PhoneResourceSender, companion::SyncStore,
                                   companion::SyncFrameSender {
    BleLink ble;
    nfc::NfcService* nfc_service = nullptr;
    storage::StorageService* storage_service = nullptr;
    EspBootstrapClock bootstrap_clock;
    EspBootstrapRandom bootstrap_random;
    std::unique_ptr<companion::PairingBootstrap> bootstrap;
    std::atomic<bool> initialized{false};
    bool initialization_started = false;
    std::atomic<bool> stop_session_task{false};
    std::atomic<bool> protocol_negotiated_local{false};
    std::atomic<bool> peer_authorized{false};
    std::atomic<uint32_t> protocol_session_id{0};
    std::atomic<uint32_t> peer_authorized_session_id{0};
    SemaphoreHandle_t session_task_done = nullptr;
    SemaphoreHandle_t clear_bonds_mutex = nullptr;
    SemaphoreHandle_t clear_bonds_done = nullptr;
    SemaphoreHandle_t resource_mutex = nullptr;
    std::atomic<bool> clear_bonds_requested{false};
    std::atomic<ConnectivityResult> clear_bonds_result{
        ConnectivityResult::kTransportError};
    std::array<uint8_t, 16> stored_companion_id{};
    bool stored_companion_id_valid = false;
    std::unique_ptr<StoredWifiCredentials> wifi_credentials;
    EspWifiBackendDriver wifi_driver;
    std::unique_ptr<WifiBackend> wifi_backend;
    std::unique_ptr<ResourceClient> resource_client;
    companion::ConnectivityConditions resource_conditions{};
    uint32_t resource_sequence = 0;
    uint32_t resource_phone_session = 0;
    uint32_t next_outbound_sequence = 1;
    std::array<uint8_t, 64> resource_payload{};
    std::array<uint8_t, companion::kMaximumFrameSize> resource_frame{};
    companion::SyncEngine sync_engine;
    companion::SyncSession sync_session{sync_engine};

    companion::StoreReadStatus Load(uint8_t* output, std::size_t capacity,
                                    std::size_t* output_size) override {
        if (storage_service == nullptr) return companion::StoreReadStatus::kError;
        std::size_t size = 0;
        const auto result = storage_service->GetBlob(kSyncStateKey, nullptr, &size);
        if (result == ESP_ERR_NOT_FOUND) return companion::StoreReadStatus::kNotFound;
        if (result != ESP_OK) return companion::StoreReadStatus::kError;
        *output_size = size;
        if (size > capacity) return companion::StoreReadStatus::kOk;
        return storage_service->GetBlob(kSyncStateKey, output, output_size) == ESP_OK
            ? companion::StoreReadStatus::kOk : companion::StoreReadStatus::kError;
    }

    bool Save(const uint8_t* input, std::size_t size) override {
        return storage_service != nullptr &&
            storage_service->SetBlob(kSyncStateKey, input, size) == ESP_OK;
    }

    companion::LinkResult SendSyncFrame(const uint8_t* frame, std::size_t size) override {
        return ble.SendForSession(protocol_session_id.load(), frame, size);
    }

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
        ConnectivityResult result = Map(ble.ClearBonds());
        if (result == ConnectivityResult::kOk) {
            peer_authorized.store(false, std::memory_order_release);
            peer_authorized_session_id.store(0, std::memory_order_release);
            xSemaphoreTake(resource_mutex, portMAX_DELAY);
            sync_session.Disconnect();
            if (storage_service != nullptr &&
                storage_service->IsInitialized()) {
                // Forget durable state before allowing a different identity.
                esp_err_t erased = storage_service->Erase(kSyncStateKey);
                if (erased == ESP_OK || erased == ESP_ERR_NOT_FOUND) {
                    if (sync_engine.Initialize(*this) == companion::SyncStatus::kOk) {
                        erased = storage_service->Erase(kCompanionIdentityKey);
                    } else erased = ESP_FAIL;
                }
                if (erased != ESP_OK && erased != ESP_ERR_NOT_FOUND) {
                    result = ConnectivityResult::kTransportError;
                    ESP_LOGW(kTag, "event=companion_reset_failed reason=%s",
                             esp_err_to_name(erased));
                } else {
                    stored_companion_id_valid = false;
                    stored_companion_id.fill(0);
                }
            } else result = ConnectivityResult::kUnavailable;
            if (bootstrap != nullptr) bootstrap->Cancel();
            if (resource_client != nullptr) {
                resource_client->PhoneDisconnected(MonotonicMilliseconds());
            }
            xSemaphoreGive(resource_mutex);
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
        bool has_proof = false;
        bool has_identity = false;
        bool has_cursors = false;
        companion::SyncCursors peer_cursors{};
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
            } else if (field.type == companion::kHelloSyncCursorsType) {
                if (has_cursors || companion::DecodeSyncCursors(
                        field.value, field.value_size, &peer_cursors) != companion::ProtocolStatus::kOk) {
                    decision.status = companion::kHelloAckStatusRejected;
                    decision.error_reason = kEnrollmentErrorSyncCursors;
                    return decision;
                }
                has_cursors = true;
            } else if (field.required) {
                LogEnrollmentRejection("unknown_required_field", session_id);
                decision.status = companion::kHelloAckStatusRejected;
                decision.error_reason = kEnrollmentErrorUnknownRequiredField;
                return decision;
            }
        }

        if (!has_cursors || (has_proof && has_identity)) {
            decision.status = companion::kHelloAckStatusRejected;
            decision.error_reason = !has_cursors ? kEnrollmentErrorSyncRequired : kEnrollmentErrorDuplicateField;
            return decision;
        }
        if (!has_proof && !has_identity) {
            ESP_LOGI(kTag, "event=hello_received session=%lu proof=absent",
                     static_cast<unsigned long>(session_id));
            decision.status = companion::kHelloAckStatusRejected;
            decision.error_reason = kEnrollmentErrorNoStoredIdentity;
            return decision;
        }

        if (has_proof && stored_companion_id_valid && !ConstantTimeEqual(
                proof_companion_id, stored_companion_id.data(), stored_companion_id.size())) {
            decision.status = companion::kHelloAckStatusRejected;
            decision.error_reason = kEnrollmentErrorIdentityMismatch;
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
                        status = bootstrap->ValidateAndPersistEnrollmentProof(
                            session_id, proof_generation, proof_token,
                            sizeof(proof_token), [&]() {
                                return PersistCompanionIdentity(proof_companion_id, proof_generation);
                            });
                        if (status == companion::BootstrapStatus::kOk) SetSessionPeerAuthorized(session_id);
                    });
                if (!session_current) {
                    LogEnrollmentRejection("session_changed", session_id);
                    peer_authorized.store(false, std::memory_order_release);
                    decision.status = companion::kHelloAckStatusRejected;
                    decision.error_reason = kEnrollmentErrorSessionBindFailed;
                } else if (status == companion::BootstrapStatus::kStoreError) {
                    peer_authorized.store(false, std::memory_order_release);
                    decision.status = companion::kHelloAckStatusRejected;
                    decision.error_reason = kEnrollmentErrorStore;
                } else if (status != companion::BootstrapStatus::kOk) {
                    LogEnrollmentRejection("invalid_proof", session_id);
                    peer_authorized.store(false, std::memory_order_release);
                    decision.status = companion::kHelloAckStatusRejected;
                    decision.error_reason = kEnrollmentErrorInvalidProof;
                } else {
                    ESP_LOGI(kTag, "event=companion_enrolled session=%lu generation=%lu",
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
                if (!ble.WithCurrentTransportSession(session_id, [&]() {
                        SetSessionPeerAuthorized(session_id);
                    })) {
                    decision.status = companion::kHelloAckStatusRejected;
                    decision.error_reason = kEnrollmentErrorSessionBindFailed;
                }
            }
        }

        if (decision.status == companion::kHelloAckStatusOk) {
            decision.peer_authorized = IsSessionPeerAuthorized(link);
            xSemaphoreTake(resource_mutex, portMAX_DELAY);
            const auto status = sync_session.Start(peer_cursors, MonotonicMilliseconds(), frame.header.sequence);
            xSemaphoreGive(resource_mutex);
            if (status != companion::SyncStatus::kOk) {
                decision.status = companion::kHelloAckStatusRejected;
                decision.peer_authorized = false;
                decision.error_reason = status == companion::SyncStatus::kStoreError ||
                    status == companion::SyncStatus::kNotInitialized ? kEnrollmentErrorStore : kEnrollmentErrorSyncCursors;
                ESP_LOGW(kTag, "event=sync_reconcile_failed status=%u", static_cast<unsigned>(status));
            }
        }
        if (decision.status != companion::kHelloAckStatusOk) peer_authorized.store(false);
        return decision;
    }

    bool PersistCompanionIdentity(const uint8_t companion_id[16],
                                  uint32_t generation) {
        if (storage_service == nullptr || !storage_service->IsInitialized()) {
            ESP_LOGW(kTag, "event=companion_identity_persist_skipped");
            return false;
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
            return false;
        }
        std::memcpy(stored_companion_id.data(), companion_id,
                    stored_companion_id.size());
        stored_companion_id_valid = true;
        ESP_LOGI(kTag, "event=companion_identity_persisted generation=%lu",
                 static_cast<unsigned long>(generation));
        return true;
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

    static uint32_t MonotonicMilliseconds() {
        return static_cast<uint32_t>(esp_timer_get_time() / 1000);
    }

    static bool DeadlineReached(uint32_t now, uint32_t deadline) {
        return static_cast<int32_t>(now - deadline) >= 0;
    }

    void ResourceSessionChanged() {
        if (resource_mutex == nullptr) return;
        xSemaphoreTake(resource_mutex, portMAX_DELAY);
        next_outbound_sequence = 1;
        sync_session.Disconnect();
        resource_phone_session = 0;
        if (resource_client != nullptr) {
            resource_client->PhoneDisconnected(MonotonicMilliseconds());
        }
        xSemaphoreGive(resource_mutex);
    }

    uint32_t NextResourceWakeMs() {
        if (resource_mutex == nullptr) return UINT32_MAX;
        xSemaphoreTake(resource_mutex, portMAX_DELAY);
        const uint32_t resource_next = resource_client == nullptr ? UINT32_MAX
            : resource_client->NextWakeMs(MonotonicMilliseconds());
        const uint32_t next = std::min(resource_next, sync_session.NextWakeMs(MonotonicMilliseconds(),
            resource_client == nullptr || !resource_client->AwaitingPhone()));
        xSemaphoreGive(resource_mutex);
        return next;
    }

    void PollResource(const BleSnapshot& link) {
        if (resource_mutex == nullptr || resource_client == nullptr) return;
        xSemaphoreTake(resource_mutex, portMAX_DELAY);
        if (IsSessionPeerAuthorized(link) && protocol_negotiated_local.load()) {
            sync_session.Poll(*this, next_outbound_sequence, MonotonicMilliseconds(),
                              !resource_client->AwaitingPhone());
            const auto status = sync_session.Status();
            if (status != companion::SyncSessionStatus::kActive &&
                status != companion::SyncSessionStatus::kDisconnected) {
                ESP_LOGW(kTag, "event=sync_session_failed session=%lu status=%u",
                         static_cast<unsigned long>(link.session_id), static_cast<unsigned>(status));
                peer_authorized.store(false);
                ble.DisconnectSession(link.session_id);
            }
        }
        resource_conditions.phone = IsSessionPeerAuthorized(link) &&
            protocol_negotiated_local.load() &&
            (sync_session.Converged() || resource_client->AwaitingPhone())
                ? companion::PhoneAvailability::kConnected
                : companion::PhoneAvailability::kUnavailable;
        resource_client->Poll(resource_conditions, MonotonicMilliseconds());
        xSemaphoreGive(resource_mutex);
    }

    // Called by ResourceClient on the session owner while resource_mutex is
    // held. Session validation is repeated by BleLink before the send.
    companion::LinkResult SendResource(
        uint32_t request_id,
        const companion::ResourceRequestMessage& request) override {
        const BleSnapshot link = ble.Snapshot();
        if (!IsSessionPeerAuthorized(link) || !protocol_negotiated_local.load() || !sync_session.Converged()) {
            return companion::LinkResult::kUnavailable;
        }
        const uint32_t sequence = next_outbound_sequence++;
        if (next_outbound_sequence == 0) next_outbound_sequence = 1;
        std::size_t payload_size = 0;
        std::size_t frame_size = 0;
        companion::FrameHeader header{};
        header.message_class = companion::MessageClass::kCommand;
        header.flags = companion::kAckRequested | companion::kRetriable;
        header.message_type = companion::kResourceRequestMessageType;
        header.request_id = request_id;
        header.sequence = sequence;
        if (companion::EncodeResourceRequestPayload(
                request, resource_payload.data(), resource_payload.size(),
                &payload_size) != companion::ProtocolStatus::kOk ||
            companion::EncodeFrame(
                header, resource_payload.data(), payload_size,
                resource_frame.data(), resource_frame.size(), &frame_size) !=
                companion::ProtocolStatus::kOk) {
            return companion::LinkResult::kInvalidArgument;
        }
        const auto result = ble.SendForSession(link.session_id,
                                               resource_frame.data(), frame_size);
        if (result == companion::LinkResult::kOk) {
            resource_sequence = sequence;
            resource_phone_session = link.session_id;
        }
        return result;
    }

    bool ProcessResourceResponse(uint32_t session_id,
                                  const companion::FrameView& frame) {
        if (frame.header.message_class != companion::MessageClass::kCommand ||
            frame.header.message_type != companion::kResourceRequestMessageType ||
            (frame.header.flags & companion::kResponse) == 0 ||
            resource_mutex == nullptr || resource_client == nullptr) return false;
        companion::ResourceResponseMessage decoded{};
        if (companion::DecodeResourceResponsePayload(
                frame.payload, frame.payload_size, &decoded) !=
                companion::ProtocolStatus::kOk) {
            decoded = {};
        }
        xSemaphoreTake(resource_mutex, portMAX_DELAY);
        bool consumed = false;
        if (resource_sequence == frame.header.sequence &&
            resource_phone_session == session_id) {
            // Lock ordering is resource -> BLE everywhere. A disconnect cannot
            // revoke authorization between this check and accepting the reply.
            ble.WithCurrentTransportSession(session_id, [&]() {
                if (peer_authorized.load() &&
                    peer_authorized_session_id.load() == session_id) {
                    consumed = resource_client->AcceptPhoneResponse(
                        frame.header.request_id, decoded, MonotonicMilliseconds());
                }
            });
        }
        xSemaphoreGive(resource_mutex);
        return consumed;
    }

    bool ProcessSyncFrame(uint32_t session_id, const companion::FrameView& frame) {
        xSemaphoreTake(resource_mutex, portMAX_DELAY);
        bool consumed = false;
        ble.WithCurrentTransportSession(session_id, [&]() {
            if (peer_authorized.load() && peer_authorized_session_id.load() == session_id) {
                consumed = sync_session.Receive(frame);
            }
        });
        xSemaphoreGive(resource_mutex);
        return consumed;
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
                self->ResourceSessionChanged();
            }
            self->ResetSessionPeerAuthorizedIfNeeded(link.session_id);
            self->PollResource(link);
            const uint32_t session_wake_ms = std::min(
                self->NextSessionWakeMs(), self->NextResourceWakeMs());

            if (link.state != BleState::kTransportReady) {
                self->protocol_negotiated_local.store(false);
                self->ble.WaitForSessionEvent(session_wake_ms);
                continue;
            }
            if (self->protocol_negotiated_local.load()) {
                ReceivedFrame received{};
                if (!self->ble.TakeReceivedFrame(&received)) {
                    self->ble.WaitForSessionEvent(std::min(
                        self->NextSessionWakeMs(),
                        self->NextResourceWakeMs()));
                    continue;
                }
                const uint32_t received_session_id = received.session_id;
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
                const companion::ProtocolStatus decoded =
                    companion::DecodeFrame(
                        received.data, received.size,
                        companion::kProtocolMajor,
                        companion::kProtocolMinor, &frame);
                const bool consumed =
                    decoded == companion::ProtocolStatus::kOk &&
                    (self->ProcessSyncFrame(received_session_id, frame) ||
                     self->ProcessResourceResponse(received_session_id, frame));
                self->ble.ReleaseReceivedFrame();
                if (!consumed) {
                    ESP_LOGW(kTag,
                             "event=protocol_frame_rejected session=%lu reason=unexpected_message",
                             static_cast<unsigned long>(link.session_id));
                }
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
                frame.header.flags == 0 && frame.header.request_id != 0 &&
                frame.header.sequence == 1;
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

            std::array<uint8_t, 8 + 4 + companion::kSyncCursorValueSize> payload{};
            companion::TlvWriter writer(payload.data(), payload.size());
            writer.Add(companion::kRequiredFieldBit | companion::kHelloAckStatusType,
                       status_value.data(), status_size);
            if (decision.status == companion::kHelloAckStatusOk && decision.peer_authorized) {
                std::array<uint8_t, companion::kSyncCursorValueSize> cursors{};
                std::size_t cursor_size = 0;
                xSemaphoreTake(self->resource_mutex, portMAX_DELAY);
                companion::EncodeSyncCursors(self->sync_engine.Cursors(), cursors.data(), cursors.size(), &cursor_size);
                xSemaphoreGive(self->resource_mutex);
                writer.Add(companion::kRequiredFieldBit | companion::kHelloSyncCursorsType,
                           cursors.data(), cursor_size);
            }
            std::array<uint8_t, companion::kFrameHeaderSize + payload.size()> response{};
            std::size_t response_size = 0;
            companion::FrameHeader header{};
            header.message_class = companion::MessageClass::kControl;
            header.flags = companion::kResponse;
            header.message_type = static_cast<uint16_t>(
                companion::ControlMessage::kHelloAck);
            header.request_id = request_id;
            header.sequence = sequence;
            if (companion::EncodeFrame(header, payload.data(), writer.Size(),
                                       response.data(), response.size(),
                                       &response_size) ==
                    companion::ProtocolStatus::kOk &&
                received_session_id == link.session_id &&
                self->ble.SendForSession(received_session_id, response.data(),
                                         response_size) ==
                    companion::LinkResult::kOk) {
                self->protocol_negotiated_local.store(decision.status == companion::kHelloAckStatusOk);
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
        if (self->resource_client != nullptr) {
            xSemaphoreTake(self->resource_mutex, portMAX_DELAY);
            self->resource_conditions.power_state = companion::ProductPowerState::kShutdown;
            self->resource_client->Cancel(MonotonicMilliseconds());
            xSemaphoreGive(self->resource_mutex);
            while (true) {
                xSemaphoreTake(self->resource_mutex, portMAX_DELAY);
                self->resource_client->Poll(self->resource_conditions, MonotonicMilliseconds());
                const bool busy = self->resource_client->WifiBusy();
                xSemaphoreGive(self->resource_mutex);
                if (!busy) break;
                vTaskDelay(pdMS_TO_TICKS(20));
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
    Stop();
    if (impl_ != nullptr) {
        if (impl_->clear_bonds_done != nullptr) {
            vSemaphoreDelete(impl_->clear_bonds_done);
            impl_->clear_bonds_done = nullptr;
        }
        if (impl_->clear_bonds_mutex != nullptr) {
            vSemaphoreDelete(impl_->clear_bonds_mutex);
            impl_->clear_bonds_mutex = nullptr;
        }
        if (impl_->resource_mutex != nullptr) {
            vSemaphoreDelete(impl_->resource_mutex);
            impl_->resource_mutex = nullptr;
        }
    }
    delete impl_;
}

ConnectivityResult ConnectivityService::Stop() {
    if (impl_ == nullptr) return ConnectivityResult::kOk;
    // Serialize with a bond-removal caller before stopping its session owner.
    if (impl_->clear_bonds_mutex != nullptr) {
        xSemaphoreTake(impl_->clear_bonds_mutex, portMAX_DELAY);
    }
    if (impl_->resource_mutex != nullptr) {
        xSemaphoreTake(impl_->resource_mutex, portMAX_DELAY);
    }
    impl_->initialized.store(false);
    impl_->stop_session_task.store(true);
    if (impl_->resource_mutex != nullptr) xSemaphoreGive(impl_->resource_mutex);
    if (impl_->clear_bonds_mutex != nullptr) xSemaphoreGive(impl_->clear_bonds_mutex);
    if (impl_->nfc_service != nullptr) impl_->nfc_service->SetEventCallback(nullptr);
    impl_->ble.WakeSessionWaiter();
    if (impl_->session_task_done != nullptr) {
        xSemaphoreTake(impl_->session_task_done, portMAX_DELAY);
        vSemaphoreDelete(impl_->session_task_done);
        impl_->session_task_done = nullptr;
    }
    impl_->ble.Stop();
    return impl_->wifi_backend != nullptr &&
        impl_->wifi_backend->State() == WifiBackendState::kStopFailed
            ? ConnectivityResult::kTransportError : ConnectivityResult::kOk;
}

void ConnectivityService::SetNfcService(nfc::NfcService* nfc_service) {
    if (impl_ == nullptr || impl_->initialization_started) return;
    impl_->nfc_service = nfc_service;
}

void ConnectivityService::SetStorageService(
    storage::StorageService* storage_service) {
    if (impl_ == nullptr || impl_->initialization_started) return;
    impl_->storage_service = storage_service;
}

ConnectivityResult ConnectivityService::Initialize() {
    if (impl_ == nullptr) return ConnectivityResult::kInvalidState;
    if (impl_->initialized.load()) return ConnectivityResult::kOk;
    if (impl_->initialization_started) return ConnectivityResult::kInvalidState;
    impl_->initialization_started = true;

    impl_->wifi_credentials.reset(new (std::nothrow)
        StoredWifiCredentials(impl_->storage_service));
    if (impl_->wifi_credentials == nullptr) return ConnectivityResult::kUnavailable;
    impl_->wifi_backend.reset(new (std::nothrow)
        WifiBackend(impl_->wifi_credentials.get(), &impl_->wifi_driver));
    if (impl_->wifi_backend == nullptr) return ConnectivityResult::kUnavailable;
    impl_->resource_client.reset(new (std::nothrow)
        ResourceClient(*impl_->wifi_backend, *impl_));
    if (impl_->resource_client == nullptr) return ConnectivityResult::kUnavailable;
    impl_->resource_conditions.battery_percent = 0;
    impl_->resource_conditions.wifi_credentials_available = impl_->wifi_credentials->Available();
    uint32_t stored_policy = 0;
    if (impl_->storage_service != nullptr &&
        impl_->storage_service->GetUInt32("conn_policy", &stored_policy) == ESP_OK &&
        stored_policy <= static_cast<uint32_t>(companion::UserConnectivityPolicy::kOffline)) {
        impl_->resource_conditions.user_policy =
            static_cast<companion::UserConnectivityPolicy>(stored_policy);
    }

    const ConnectivityResult result = Map(impl_->ble.Initialize());
    if (result != ConnectivityResult::kOk) return result;

    impl_->LoadCompanionIdentity();
    const auto sync_status = impl_->sync_engine.Initialize(*impl_);
    if (sync_status != companion::SyncStatus::kOk) {
        ESP_LOGW(kTag, "event=sync_store_load status=%u", static_cast<unsigned>(sync_status));
    }

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
    impl_->resource_mutex = xSemaphoreCreateMutex();
    if (impl_->clear_bonds_mutex == nullptr ||
        impl_->clear_bonds_done == nullptr || impl_->resource_mutex == nullptr) {
        if (impl_->clear_bonds_mutex != nullptr) {
            vSemaphoreDelete(impl_->clear_bonds_mutex);
            impl_->clear_bonds_mutex = nullptr;
        }
        if (impl_->clear_bonds_done != nullptr) {
            vSemaphoreDelete(impl_->clear_bonds_done);
            impl_->clear_bonds_done = nullptr;
        }
        if (impl_->resource_mutex != nullptr) {
            vSemaphoreDelete(impl_->resource_mutex);
            impl_->resource_mutex = nullptr;
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
        vSemaphoreDelete(impl_->resource_mutex);
        impl_->resource_mutex = nullptr;
        impl_->ble.Stop();
        if (impl_->nfc_service != nullptr) {
            impl_->nfc_service->SetEventCallback(nullptr);
        }
        return ConnectivityResult::kUnavailable;
    }
    if (xTaskCreate(&Impl::SessionTask, "zectrix_session", 8192, impl_, 4,
                    nullptr) != pdPASS) {
        vSemaphoreDelete(impl_->session_task_done);
        impl_->session_task_done = nullptr;
        vSemaphoreDelete(impl_->clear_bonds_mutex);
        vSemaphoreDelete(impl_->clear_bonds_done);
        impl_->clear_bonds_mutex = nullptr;
        impl_->clear_bonds_done = nullptr;
        vSemaphoreDelete(impl_->resource_mutex);
        impl_->resource_mutex = nullptr;
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
    if (!impl_->initialized.load()) {
        xSemaphoreGive(impl_->clear_bonds_mutex);
        return ConnectivityResult::kInvalidState;
    }
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
    if (impl_->resource_mutex != nullptr && impl_->resource_client != nullptr) {
        xSemaphoreTake(impl_->resource_mutex, portMAX_DELAY);
        snapshot.wifi_credentials_available = impl_->resource_conditions.wifi_credentials_available;
        snapshot.resource_busy = impl_->resource_client->Busy();
        snapshot.wifi_state = impl_->resource_client->WifiState();
        snapshot.resource_decision = impl_->resource_client->Decision();
        snapshot.sync_converged = snapshot.peer_authorized && snapshot.protocol_negotiated_local &&
            impl_->sync_session.Converged();
        snapshot.pending_durable_states = impl_->sync_engine.PendingDurableCount();
        xSemaphoreGive(impl_->resource_mutex);
    }
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

void ConnectivityService::UpdatePower(
    const power::PowerSnapshot& power, companion::ProductPowerState state) {
    if (impl_ == nullptr || impl_->resource_mutex == nullptr) return;
    xSemaphoreTake(impl_->resource_mutex, portMAX_DELAY);
    impl_->resource_conditions.external_power = power.external_power_present;
    impl_->resource_conditions.battery_percent = power.battery_valid && !power.battery_absent
        ? power.battery_percent : 0;
    impl_->resource_conditions.power_state = state;
    xSemaphoreGive(impl_->resource_mutex);
    impl_->ble.WakeSessionWaiter();
}

ConnectivityResult ConnectivityService::SetUserPolicy(companion::UserConnectivityPolicy policy) {
    if (impl_ == nullptr || !impl_->initialized.load() ||
        policy > companion::UserConnectivityPolicy::kOffline) return ConnectivityResult::kInvalidState;
    xSemaphoreTake(impl_->resource_mutex, portMAX_DELAY);
    if (!impl_->initialized.load()) {
        xSemaphoreGive(impl_->resource_mutex);
        return ConnectivityResult::kInvalidState;
    }
    const esp_err_t saved = impl_->storage_service == nullptr ? ESP_ERR_INVALID_STATE
        : impl_->storage_service->SetUInt32("conn_policy", static_cast<uint32_t>(policy));
    if (saved == ESP_OK) impl_->resource_conditions.user_policy = policy;
    xSemaphoreGive(impl_->resource_mutex);
    impl_->ble.WakeSessionWaiter();
    return saved == ESP_OK ? ConnectivityResult::kOk : ConnectivityResult::kUnavailable;
}

ConnectivityResult ConnectivityService::ConfigureWifi(const WifiCredentials& credentials) {
    if (impl_ == nullptr || !impl_->initialized.load() ||
        impl_->wifi_credentials == nullptr) return ConnectivityResult::kInvalidState;
    if (!ValidateWifiCredentials(credentials)) return ConnectivityResult::kInvalidState;
    xSemaphoreTake(impl_->resource_mutex, portMAX_DELAY);
    if (!impl_->initialized.load()) {
        xSemaphoreGive(impl_->resource_mutex);
        return ConnectivityResult::kInvalidState;
    }
    if (impl_->resource_client->WifiBusy()) {
        xSemaphoreGive(impl_->resource_mutex);
        return ConnectivityResult::kBusy;
    }
    const esp_err_t saved = impl_->wifi_credentials->Save(credentials);
    if (saved == ESP_OK) impl_->resource_conditions.wifi_credentials_available = true;
    xSemaphoreGive(impl_->resource_mutex);
    impl_->ble.WakeSessionWaiter();
    return saved == ESP_OK ? ConnectivityResult::kOk : ConnectivityResult::kUnavailable;
}

ConnectivityResult ConnectivityService::RequestResource(
    const companion::ResourceRequestMessage& request) {
    if (impl_ == nullptr || !impl_->initialized.load() ||
        impl_->resource_mutex == nullptr || impl_->resource_client == nullptr) {
        return ConnectivityResult::kInvalidState;
    }
    if (request.maximum_response_bytes == 0 ||
        request.maximum_response_bytes > companion::kResourceMaximumBodySize ||
        request.timeout_ms < companion::kResourceMinimumTimeoutMs ||
        request.timeout_ms > companion::kResourceMaximumTimeoutMs) {
        return ConnectivityResult::kInvalidState;
    }
    xSemaphoreTake(impl_->resource_mutex, portMAX_DELAY);
    if (!impl_->initialized.load()) {
        xSemaphoreGive(impl_->resource_mutex);
        return ConnectivityResult::kInvalidState;
    }
    uint32_t request_id = esp_random();
    if (request_id == 0) request_id = 1;
    const bool accepted = impl_->resource_client->Begin(
        request_id, request, Impl::MonotonicMilliseconds());
    xSemaphoreGive(impl_->resource_mutex);
    impl_->ble.WakeSessionWaiter();
    return accepted ? ConnectivityResult::kOk : ConnectivityResult::kBusy;
}

bool ConnectivityService::TakeResourceResponse(ResourceResponse* response) {
    if (impl_ == nullptr || response == nullptr ||
        impl_->resource_mutex == nullptr || impl_->resource_client == nullptr) return false;
    xSemaphoreTake(impl_->resource_mutex, portMAX_DELAY);
    const bool received = impl_->resource_client->TakeResponse(response);
    xSemaphoreGive(impl_->resource_mutex);
    if (received) impl_->ble.WakeSessionWaiter();
    return received;
}

companion::SyncStatus ConnectivityService::PutDurableState(
    uint16_t key, uint32_t revision, const uint8_t* value, std::size_t size) {
    if (impl_ == nullptr || !impl_->initialized.load() || impl_->resource_mutex == nullptr) {
        return companion::SyncStatus::kNotInitialized;
    }
    xSemaphoreTake(impl_->resource_mutex, portMAX_DELAY);
    const auto status = impl_->sync_engine.PutDurableState(key, revision, value, size);
    xSemaphoreGive(impl_->resource_mutex);
    if (status == companion::SyncStatus::kOk) impl_->ble.WakeSessionWaiter();
    return status;
}

companion::SyncStatus ConnectivityService::ReadDurableState(
    uint16_t key, uint32_t* revision, uint8_t* value, std::size_t* size) const {
    if (impl_ == nullptr || !impl_->initialized.load() || impl_->resource_mutex == nullptr) {
        return companion::SyncStatus::kNotInitialized;
    }
    if (revision == nullptr || size == nullptr || value == nullptr) return companion::SyncStatus::kInvalidArgument;
    xSemaphoreTake(impl_->resource_mutex, portMAX_DELAY);
    companion::DurableStateView state{};
    auto status = impl_->sync_engine.ReadIncomingState(key, &state);
    if (status == companion::SyncStatus::kOk) {
        if (*size < state.value_size) status = companion::SyncStatus::kValueTooLarge;
        else {
            *revision = state.revision;
            std::memcpy(value, state.value, state.value_size);
        }
        *size = state.value_size;
    }
    xSemaphoreGive(impl_->resource_mutex);
    return status;
}

}  // namespace zectrix::connectivity
