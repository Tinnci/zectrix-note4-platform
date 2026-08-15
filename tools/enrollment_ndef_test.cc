#include "zectrix_enrollment_ndef.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using zectrix::companion::EnrollmentNdefPayload;
using zectrix::companion::EnrollmentNdefStatus;

void TestRoundTrip() {
    EnrollmentNdefPayload original{};
    original.generation = 0x12345678;
    for (std::size_t i = 0; i < original.token.size(); ++i) {
        original.token[i] = static_cast<uint8_t>(0x10 + i);
    }
    original.flags = zectrix::companion::kEnrollmentNdefFlagBleAddressValid;
    original.ble_role = zectrix::companion::kEnrollmentNdefBleRolePeripheral;
    original.ble_address_type = 0;
    original.ble_address = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    for (std::size_t i = 0; i < original.device_id.size(); ++i) {
        original.device_id[i] = static_cast<uint8_t>(0xa0 + i);
    }

    std::vector<uint8_t> message(
        zectrix::companion::EnrollmentNdefMessageSize());
    std::size_t encoded_size = 0;
    assert(zectrix::companion::EncodeEnrollmentNdefMessage(
               original, message.data(), message.size(), &encoded_size) ==
           EnrollmentNdefStatus::kOk);
    assert(encoded_size == message.size());
    assert(message.size() ==
           3 + zectrix::companion::kEnrollmentNdefMimeTypeLength +
               zectrix::companion::kEnrollmentNdefPayloadSize);

    EnrollmentNdefPayload decoded{};
    assert(zectrix::companion::DecodeEnrollmentNdefMessage(
               message.data(), message.size(), &decoded) ==
           EnrollmentNdefStatus::kOk);
    assert(decoded.version == original.version);
    assert(decoded.flags == original.flags);
    assert(decoded.ble_role == original.ble_role);
    assert(decoded.ble_address_type == original.ble_address_type);
    assert(decoded.ble_address == original.ble_address);
    assert(decoded.device_id == original.device_id);
    assert(decoded.generation == original.generation);
    assert(decoded.token == original.token);
}

void TestRejections() {
    EnrollmentNdefPayload original{};
    original.generation = 1;
    original.token.fill(0x5a);

    std::vector<uint8_t> message(
        zectrix::companion::EnrollmentNdefMessageSize());
    std::size_t encoded_size = 0;
    assert(zectrix::companion::EncodeEnrollmentNdefMessage(
               original, message.data(), message.size(), &encoded_size) ==
           EnrollmentNdefStatus::kOk);

    EnrollmentNdefPayload decoded{};

    // Truncated message.
    assert(zectrix::companion::DecodeEnrollmentNdefMessage(
               message.data(), message.size() - 1, &decoded) ==
           EnrollmentNdefStatus::kTruncated);

    // Oversized message.
    std::vector<uint8_t> oversized = message;
    oversized.push_back(0);
    assert(zectrix::companion::DecodeEnrollmentNdefMessage(
               oversized.data(), oversized.size(), &decoded) ==
           EnrollmentNdefStatus::kOversized);

    // Wrong NDEF record type.
    std::vector<uint8_t> wrong_type = message;
    wrong_type[0] = 0xD1;  // TNF=well-known instead of MIME
    assert(zectrix::companion::DecodeEnrollmentNdefMessage(
               wrong_type.data(), wrong_type.size(), &decoded) ==
           EnrollmentNdefStatus::kBadType);

    // Bad payload magic.
    std::vector<uint8_t> bad_magic = message;
    const std::size_t payload_offset =
        3 + zectrix::companion::kEnrollmentNdefMimeTypeLength;
    bad_magic[payload_offset] ^= 0xff;
    assert(zectrix::companion::DecodeEnrollmentNdefMessage(
               bad_magic.data(), bad_magic.size(), &decoded) ==
           EnrollmentNdefStatus::kBadMagic);

    // Unsupported payload version.
    std::vector<uint8_t> bad_version = message;
    bad_version[payload_offset + 4] = 2;
    assert(zectrix::companion::DecodeEnrollmentNdefMessage(
               bad_version.data(), bad_version.size(), &decoded) ==
           EnrollmentNdefStatus::kUnsupportedVersion);

    // Invalid payload flags.
    EnrollmentNdefPayload bad_flags = original;
    bad_flags.flags = 0x80;
    assert(zectrix::companion::EncodeEnrollmentNdefMessage(
               bad_flags, message.data(), message.size(), &encoded_size) ==
           EnrollmentNdefStatus::kInvalidArgument);

    // Null argument handling.
    assert(zectrix::companion::EncodeEnrollmentNdefMessage(
               original, nullptr, message.size(), &encoded_size) ==
           EnrollmentNdefStatus::kInvalidArgument);
    assert(zectrix::companion::DecodeEnrollmentNdefMessage(
               nullptr, message.size(), &decoded) ==
           EnrollmentNdefStatus::kInvalidArgument);
}

}  // namespace

int main() {
    TestRoundTrip();
    TestRejections();
    return 0;
}
