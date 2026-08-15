#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "esp_err.h"
#include "zectrix_enrollment_ndef.h"

class ZectrixNfc;

namespace zectrix::nfc {

enum class NfcFieldEvent : uint8_t {
    kRising = 0,
};

struct NfcSnapshot {
    bool initialized = false;
    bool powered = false;
    bool field_present = false;
    bool enrollment_ndef_prepared = false;
    uint32_t enrollment_generation = 0;
};

// Internal platform owner for the board NFC device. It is not part of SDK v1.
// The service only queues field observations and prepares NDEF records; it
// never performs I2C, BLE or protocol work from the field callback.
class NfcService {
public:
    // Attaches to an initialized ZectrixNfc board object and registers the
    // service field callback. The board must outlive this service.
    static esp_err_t Attach(ZectrixNfc& nfc, NfcService** out_service);
    ~NfcService();

    NfcService(const NfcService&) = delete;
    NfcService& operator=(const NfcService&) = delete;

    // Builds and writes the v1 enrollment NDEF record. The token is never
    // stored in this service. Returns ESP_OK after the board accepts the
    // complete Type 2 Tag NDEF TLV write.
    esp_err_t PrepareEnrollmentNdef(uint32_t generation,
                                    const std::array<uint8_t, 16>& token);

    // Erases the NFC user data area. Use only when no other user data must be
    // preserved; the self-test backup path owns ordinary user-data recovery.
    esp_err_t ClearEnrollmentNdef();

    NfcSnapshot Snapshot() const;

    // Consumes a coalesced field event. The implementation currently reports
    // rising edges only; callers must inspect Snapshot() for the current
    // field level after each event.
    bool TakeFieldEvent(NfcFieldEvent* event);

private:
    explicit NfcService(ZectrixNfc& nfc) : nfc_(&nfc) {}

    void OnFieldChanged(bool present);

    ZectrixNfc* nfc_ = nullptr;
    std::atomic<bool> field_present_{false};
    std::atomic<bool> field_rising_pending_{false};
    std::atomic<bool> enrollment_ndef_prepared_{false};
    std::atomic<uint32_t> enrollment_generation_{0};
};

}  // namespace zectrix::nfc
