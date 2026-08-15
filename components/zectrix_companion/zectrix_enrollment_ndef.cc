#include "zectrix_enrollment_ndef.h"

#include <cstring>

namespace zectrix::companion {
namespace {

void PutUInt32(uint8_t* output, uint32_t value) {
    output[0] = static_cast<uint8_t>(value & 0xffU);
    output[1] = static_cast<uint8_t>((value >> 8U) & 0xffU);
    output[2] = static_cast<uint8_t>((value >> 16U) & 0xffU);
    output[3] = static_cast<uint8_t>((value >> 24U) & 0xffU);
}

uint32_t GetUInt32(const uint8_t* input) {
    return static_cast<uint32_t>(input[0]) |
           static_cast<uint32_t>(input[1]) << 8U |
           static_cast<uint32_t>(input[2]) << 16U |
           static_cast<uint32_t>(input[3]) << 24U;
}

void EncodePayload(const EnrollmentNdefPayload& payload, uint8_t* output) {
    output[0] = static_cast<uint8_t>(kEnrollmentNdefMagic & 0xffU);
    output[1] = static_cast<uint8_t>((kEnrollmentNdefMagic >> 8U) & 0xffU);
    output[2] = static_cast<uint8_t>((kEnrollmentNdefMagic >> 16U) & 0xffU);
    output[3] = static_cast<uint8_t>((kEnrollmentNdefMagic >> 24U) & 0xffU);
    output[4] = payload.version;
    output[5] = payload.flags;
    output[6] = payload.ble_role;
    output[7] = payload.ble_address_type;
    std::memcpy(output + 8, payload.ble_address.data(), payload.ble_address.size());
    std::memcpy(output + 14, payload.device_id.data(), payload.device_id.size());
    PutUInt32(output + 30, payload.generation);
    std::memcpy(output + 34, payload.token.data(), payload.token.size());
}

EnrollmentNdefStatus DecodePayload(const uint8_t* input, std::size_t size,
                                   EnrollmentNdefPayload* payload) {
    if (size != kEnrollmentNdefPayloadSize) {
        return size < kEnrollmentNdefPayloadSize
                   ? EnrollmentNdefStatus::kTruncated
                   : EnrollmentNdefStatus::kOversized;
    }
    if (GetUInt32(input) != kEnrollmentNdefMagic) {
        return EnrollmentNdefStatus::kBadMagic;
    }
    if (input[4] != kEnrollmentNdefVersion) {
        return EnrollmentNdefStatus::kUnsupportedVersion;
    }
    payload->version = input[4];
    payload->flags = input[5];
    payload->ble_role = input[6];
    payload->ble_address_type = input[7];
    std::memcpy(payload->ble_address.data(), input + 8,
                payload->ble_address.size());
    std::memcpy(payload->device_id.data(), input + 14,
                payload->device_id.size());
    payload->generation = GetUInt32(input + 30);
    std::memcpy(payload->token.data(), input + 34, payload->token.size());
    return EnrollmentNdefStatus::kOk;
}

}  // namespace

std::size_t EnrollmentNdefMessageSize() {
    // NDEF record header: flag byte, type length, payload length, type, payload.
    return 1 + 1 + 1 + kEnrollmentNdefMimeTypeLength +
           kEnrollmentNdefPayloadSize;
}

EnrollmentNdefStatus EncodeEnrollmentNdefMessage(
    const EnrollmentNdefPayload& payload, uint8_t* output,
    std::size_t output_capacity, std::size_t* output_size) {
    if (output == nullptr || output_size == nullptr) {
        return EnrollmentNdefStatus::kInvalidArgument;
    }
    *output_size = 0;
    if (payload.version != kEnrollmentNdefVersion) {
        return EnrollmentNdefStatus::kUnsupportedVersion;
    }
    if ((payload.flags & ~kEnrollmentNdefFlagBleAddressValid) != 0) {
        return EnrollmentNdefStatus::kInvalidArgument;
    }
    const std::size_t message_size = EnrollmentNdefMessageSize();
    if (output_capacity < message_size) {
        return EnrollmentNdefStatus::kBufferTooSmall;
    }
    std::size_t offset = 0;
    // MB=1, ME=1, SR=1, IL=0, TNF=0x02 (MIME-type).
    output[offset++] = 0xD2;
    output[offset++] = static_cast<uint8_t>(kEnrollmentNdefMimeTypeLength);
    output[offset++] = static_cast<uint8_t>(kEnrollmentNdefPayloadSize);
    std::memcpy(output + offset, kEnrollmentNdefMimeType,
                kEnrollmentNdefMimeTypeLength);
    offset += kEnrollmentNdefMimeTypeLength;
    EncodePayload(payload, output + offset);
    offset += kEnrollmentNdefPayloadSize;
    *output_size = offset;
    return EnrollmentNdefStatus::kOk;
}

EnrollmentNdefStatus DecodeEnrollmentNdefMessage(
    const uint8_t* message, std::size_t message_size,
    EnrollmentNdefPayload* payload) {
    if (message == nullptr || payload == nullptr) {
        return EnrollmentNdefStatus::kInvalidArgument;
    }
    *payload = {};
    if (message_size != EnrollmentNdefMessageSize()) {
        return message_size < EnrollmentNdefMessageSize()
                   ? EnrollmentNdefStatus::kTruncated
                   : EnrollmentNdefStatus::kOversized;
    }
    if (message[0] != 0xD2 || message[1] != kEnrollmentNdefMimeTypeLength ||
        message[2] != kEnrollmentNdefPayloadSize) {
        return EnrollmentNdefStatus::kBadType;
    }
    if (std::memcmp(message + 3, kEnrollmentNdefMimeType,
                    kEnrollmentNdefMimeTypeLength) != 0) {
        return EnrollmentNdefStatus::kBadType;
    }
    return DecodePayload(message + 3 + kEnrollmentNdefMimeTypeLength,
                         kEnrollmentNdefPayloadSize, payload);
}

}  // namespace zectrix::companion
