#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "zectrix_pairing_bootstrap.h"

namespace zectrix::companion {

// NDEF v1 draft for NFC-assisted companion enrollment.
//
// Record type: MIME `application/vnd.zectrix.enroll.v1` (TNF=0x02).
// The raw NDEF message is written through the platform NFC owner, which wraps
// it in a Type 2 Tag NDEF TLV before programming the user memory.
//
// Payload layout (50 bytes, little-endian for multi-byte integers):
//   0..3    magic "ZEN1" (5a 45 4e 31 on wire)
//   4       format version, currently 1
//   5       flags; bit 0 means the BLE address field is valid
//   6       BLE role; 1 = peripheral, 2 = central
//   7       BLE address type; 0 = public, 1 = random static, 0xff = unknown
//   8..13   BLE address in display order (most significant octet first), or
//           zero when unknown
//   14..29  device identity; zero until a platform identity owner is selected
//   30..33  enrollment generation, uint32 LE
//   34..49  enrollment token, 128-bit, byte order as generated
constexpr char kEnrollmentNdefMimeType[] =
    "application/vnd.zectrix.enroll.v1";
constexpr std::size_t kEnrollmentNdefMimeTypeLength =
    sizeof(kEnrollmentNdefMimeType) - 1;
constexpr std::size_t kEnrollmentNdefPayloadSize = 50;
constexpr uint32_t kEnrollmentNdefMagic = 0x314e455aU;  // "ZEN1" LE
constexpr uint8_t kEnrollmentNdefVersion = 1;
constexpr uint8_t kEnrollmentNdefFlagBleAddressValid = 1U << 0;
constexpr uint8_t kEnrollmentNdefBleRolePeripheral = 1;
constexpr uint8_t kEnrollmentNdefBleRoleCentral = 2;
constexpr uint8_t kEnrollmentNdefBleAddressTypeUnknown = 0xff;

struct EnrollmentNdefPayload {
    uint8_t version = kEnrollmentNdefVersion;
    uint8_t flags = 0;
    uint8_t ble_role = kEnrollmentNdefBleRolePeripheral;
    uint8_t ble_address_type = kEnrollmentNdefBleAddressTypeUnknown;
    std::array<uint8_t, 6> ble_address{};
    std::array<uint8_t, 16> device_id{};
    uint32_t generation = 0;
    BootstrapToken token{};
};

enum class EnrollmentNdefStatus : uint8_t {
    kOk = 0,
    kInvalidArgument,
    kBufferTooSmall,
    kBadMagic,
    kUnsupportedVersion,
    kTruncated,
    kOversized,
    kBadType,
    kInvalidPayload,
};

std::size_t EnrollmentNdefMessageSize();

// Serializes the payload into the raw NDEF message bytes.
EnrollmentNdefStatus EncodeEnrollmentNdefMessage(
    const EnrollmentNdefPayload& payload, uint8_t* output,
    std::size_t output_capacity, std::size_t* output_size);

// Parses a raw NDEF message and returns the enrollment payload. The parser
// rejects truncated, oversized or wrong-type records.
EnrollmentNdefStatus DecodeEnrollmentNdefMessage(
    const uint8_t* message, std::size_t message_size,
    EnrollmentNdefPayload* payload);

}  // namespace zectrix::companion
