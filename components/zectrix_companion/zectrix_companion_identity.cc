#include "zectrix_companion_identity.h"

#include <cstring>

namespace zectrix::companion {
namespace {

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

}  // namespace

void EncodeCompanionIdentityRecord(const CompanionIdentityRecord& record,
                                   uint8_t* output) {
    if (output == nullptr) return;
    PutUInt32(output, record.magic);
    PutUInt16(output + 4, record.version);
    PutUInt16(output + 6, record.reserved);
    std::memcpy(output + 8, record.companion_id, sizeof(record.companion_id));
    PutUInt32(output + 24, record.enrollment_generation);
}

bool DecodeCompanionIdentityRecord(const uint8_t* input, std::size_t size,
                                   CompanionIdentityRecord* record) {
    if (input == nullptr || record == nullptr ||
        size != kCompanionIdentityRecordSize) {
        return false;
    }
    record->magic = GetUInt32(input);
    record->version = GetUInt16(input + 4);
    record->reserved = GetUInt16(input + 6);
    std::memcpy(record->companion_id, input + 8,
                sizeof(record->companion_id));
    record->enrollment_generation = GetUInt32(input + 24);
    return true;
}

}  // namespace zectrix::companion
