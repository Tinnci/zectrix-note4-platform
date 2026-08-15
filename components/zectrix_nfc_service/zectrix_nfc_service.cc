#include "zectrix_nfc_service.h"

#include <new>
#include <utility>
#include <vector>

#include "zectrix_nfc.h"

namespace zectrix::nfc {

esp_err_t NfcService::Attach(ZectrixNfc& nfc, NfcService** out_service) {
    if (out_service == nullptr) return ESP_ERR_INVALID_ARG;
    *out_service = nullptr;
    auto* service = new (std::nothrow) NfcService(nfc);
    if (service == nullptr) return ESP_ERR_NO_MEM;
    *out_service = service;
    service->field_present_.store(nfc.HasField(), std::memory_order_release);
    nfc.SetFieldCallback([service](bool present) {
        service->OnFieldChanged(present);
    });
    return ESP_OK;
}

NfcService::~NfcService() {
    SetEventCallback(nullptr);
    if (nfc_ != nullptr) {
        nfc_->SetFieldCallback(nullptr);
    }
}

esp_err_t NfcService::PrepareEnrollmentNdef(
    uint32_t generation, const std::array<uint8_t, 16>& token,
    const EnrollmentNdefInfo& info) {
    if (nfc_ == nullptr || generation == 0) return ESP_ERR_INVALID_ARG;
    bool all_zero = true;
    for (uint8_t value : token) {
        if (value != 0) {
            all_zero = false;
            break;
        }
    }
    if (all_zero) return ESP_ERR_INVALID_ARG;

    companion::EnrollmentNdefPayload payload{};
    payload.generation = generation;
    payload.token = token;
    if (info.ble_address_type !=
        companion::kEnrollmentNdefBleAddressTypeUnknown) {
        payload.flags |= companion::kEnrollmentNdefFlagBleAddressValid;
    }
    payload.ble_address_type = info.ble_address_type;
    payload.ble_address = info.ble_address;
    payload.device_id = info.device_id;

    std::vector<uint8_t> message(companion::EnrollmentNdefMessageSize());
    std::size_t message_size = 0;
    const companion::EnrollmentNdefStatus status =
        companion::EncodeEnrollmentNdefMessage(
            payload, message.data(), message.size(), &message_size);
    if (status != companion::EnrollmentNdefStatus::kOk) {
        return ESP_ERR_INVALID_ARG;
    }
    message.resize(message_size);

    const esp_err_t err = nfc_->WriteNdef(message);
    if (err != ESP_OK) return err;
    enrollment_ndef_prepared_.store(true, std::memory_order_release);
    enrollment_generation_.store(generation, std::memory_order_release);
    return ESP_OK;
}

esp_err_t NfcService::ClearEnrollmentNdef() {
    if (nfc_ == nullptr) return ESP_ERR_INVALID_STATE;
    const esp_err_t err =
        nfc_->ClearUserData(0, nfc_->GetUserDataCapacity());
    if (err == ESP_OK) {
        enrollment_ndef_prepared_.store(false, std::memory_order_release);
        enrollment_generation_.store(0, std::memory_order_release);
    }
    return err;
}

void NfcService::SetEventCallback(std::function<void()> callback) {
    std::unique_lock<std::mutex> lock(event_callback_mutex_);
    event_callback_ = std::move(callback);
    event_callback_idle_cv_.wait(lock, [this]() {
        return event_callback_in_flight_ == 0;
    });
}

NfcSnapshot NfcService::Snapshot() const {
    NfcSnapshot snapshot{};
    snapshot.initialized = true;
    snapshot.powered = nfc_ != nullptr && nfc_->IsPowered();
    snapshot.field_present = field_present_.load(std::memory_order_acquire);
    snapshot.enrollment_ndef_prepared =
        enrollment_ndef_prepared_.load(std::memory_order_acquire);
    snapshot.enrollment_generation =
        enrollment_generation_.load(std::memory_order_acquire);
    return snapshot;
}

bool NfcService::TakeFieldEvent(NfcFieldEvent* event) {
    if (event == nullptr) return false;
    *event = NfcFieldEvent::kRising;
    return field_rising_pending_.exchange(false, std::memory_order_acq_rel);
}

void NfcService::OnFieldChanged(bool present) {
    const bool previous = field_present_.exchange(present, std::memory_order_acq_rel);
    if (present && !previous) {
        field_rising_pending_.store(true, std::memory_order_release);
    }
    std::function<void()> callback;
    {
        std::lock_guard<std::mutex> lock(event_callback_mutex_);
        callback = event_callback_;
        if (callback) ++event_callback_in_flight_;
    }
    if (callback) {
        callback();
        {
            std::lock_guard<std::mutex> lock(event_callback_mutex_);
            --event_callback_in_flight_;
        }
        event_callback_idle_cv_.notify_all();
    }
}

}  // namespace zectrix::nfc
