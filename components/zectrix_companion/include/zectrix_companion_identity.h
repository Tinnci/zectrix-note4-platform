#pragma once

#include <cstddef>
#include <cstdint>

namespace zectrix::companion {

// Explicit versioned storage record for the authorized companion identity.
// This is a firmware persistence format, not a wire frame.
struct CompanionIdentityRecord {
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t reserved = 0;
    uint8_t companion_id[16] = {};
    uint32_t enrollment_generation = 0;
};

inline constexpr std::size_t kCompanionIdentityRecordSize =
    sizeof(CompanionIdentityRecord);

void EncodeCompanionIdentityRecord(const CompanionIdentityRecord& record,
                                   uint8_t* output);

bool DecodeCompanionIdentityRecord(const uint8_t* input, std::size_t size,
                                   CompanionIdentityRecord* record);

}  // namespace zectrix::companion
