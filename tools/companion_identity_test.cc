#include "zectrix_companion_identity.h"

#include <array>
#include <cassert>
#include <cstring>

int main() {
    using zectrix::companion::CompanionIdentityRecord;
    using zectrix::companion::DecodeCompanionIdentityRecord;
    using zectrix::companion::EncodeCompanionIdentityRecord;

    static_assert(zectrix::companion::kCompanionIdentityRecordSize == 28);

    CompanionIdentityRecord original{};
    original.magic = 0x3150435aU;
    original.version = 1;
    original.reserved = 0;
    for (std::size_t i = 0; i < sizeof(original.companion_id); ++i) {
        original.companion_id[i] = static_cast<uint8_t>(0x10 + i);
    }
    original.enrollment_generation = 0x89abcdefU;

    std::array<uint8_t, zectrix::companion::kCompanionIdentityRecordSize>
        encoded{};
    EncodeCompanionIdentityRecord(original, encoded.data());

    // companion_id occupies bytes 8..23 and must not overlap generation.
    assert(encoded[8] == 0x10);
    assert(encoded[23] == 0x1f);
    assert(encoded[24] == 0xef);
    assert(encoded[25] == 0xcd);
    assert(encoded[26] == 0xab);
    assert(encoded[27] == 0x89);

    CompanionIdentityRecord decoded{};
    assert(DecodeCompanionIdentityRecord(
        encoded.data(), encoded.size(), &decoded));
    assert(decoded.magic == original.magic);
    assert(decoded.version == original.version);
    assert(decoded.reserved == original.reserved);
    assert(std::memcmp(decoded.companion_id, original.companion_id,
                       sizeof(original.companion_id)) == 0);
    assert(decoded.enrollment_generation == original.enrollment_generation);

    assert(!DecodeCompanionIdentityRecord(encoded.data(), encoded.size() - 1,
                                          &decoded));
    assert(!DecodeCompanionIdentityRecord(encoded.data(), encoded.size() + 1,
                                          &decoded));
    return 0;
}
