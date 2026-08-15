#include "zectrix_companion_protocol.h"

#include <algorithm>
#include <cstring>

namespace zectrix::companion {
namespace {

constexpr uint8_t kAllowedFrameFlags =
    kAckRequested | kResponse | kRetriable;
constexpr uint8_t kFragmentMagic = 0xa7;
constexpr uint8_t kFragmentVersion = 1;
constexpr uint8_t kFragmentStart = 1U << 0;
constexpr uint8_t kFragmentEnd = 1U << 1;

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

bool IsValidMessageClass(uint8_t value) {
    return value <= static_cast<uint8_t>(MessageClass::kStream);
}

uint32_t UpdateCrc32(uint32_t crc, const uint8_t* data, std::size_t size) {
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return crc;
}

}  // namespace

uint32_t Crc32(const uint8_t* data, std::size_t size, uint32_t previous) {
    if (data == nullptr && size != 0) return 0;
    uint32_t crc = previous ^ 0xffffffffU;
    crc = UpdateCrc32(crc, data, size);
    return crc ^ 0xffffffffU;
}

ProtocolStatus EncodeFrame(const FrameHeader& header, const uint8_t* payload,
                           std::size_t payload_size, uint8_t* output,
                           std::size_t output_capacity,
                           std::size_t* output_size) {
    if (output_size == nullptr || output == nullptr ||
        (payload == nullptr && payload_size != 0)) {
        return ProtocolStatus::kInvalidArgument;
    }
    *output_size = 0;
    if (!IsValidMessageClass(static_cast<uint8_t>(header.message_class))) {
        return ProtocolStatus::kInvalidMessageClass;
    }
    if ((header.flags & ~kAllowedFrameFlags) != 0) {
        return ProtocolStatus::kInvalidFlags;
    }
    if (payload_size > kMaximumPayloadSize) return ProtocolStatus::kOversized;
    const std::size_t frame_size = kFrameHeaderSize + payload_size;
    if (output_capacity < frame_size) return ProtocolStatus::kBufferTooSmall;

    PutUInt16(output, kFrameMagic);
    output[2] = header.major;
    output[3] = header.minor;
    output[4] = static_cast<uint8_t>(header.message_class);
    output[5] = header.flags;
    PutUInt16(output + 6, header.message_type);
    PutUInt32(output + 8, header.request_id);
    PutUInt32(output + 12, header.sequence);
    PutUInt16(output + 16, static_cast<uint16_t>(payload_size));
    PutUInt16(output + 18, 0);
    PutUInt32(output + 20, 0);
    if (payload_size != 0) {
        std::memcpy(output + kFrameHeaderSize, payload, payload_size);
    }

    uint32_t crc = Crc32(output, 20);
    crc = Crc32(output + kFrameHeaderSize, payload_size, crc);
    PutUInt32(output + 20, crc);
    *output_size = frame_size;
    return ProtocolStatus::kOk;
}

ProtocolStatus DecodeFrame(const uint8_t* frame, std::size_t frame_size,
                           uint8_t supported_major,
                           uint8_t maximum_supported_minor,
                           FrameView* output) {
    if (frame == nullptr || output == nullptr) {
        return ProtocolStatus::kInvalidArgument;
    }
    *output = {};
    if (frame_size < kFrameHeaderSize) return ProtocolStatus::kTruncated;
    if (GetUInt16(frame) != kFrameMagic) return ProtocolStatus::kBadMagic;
    if (frame[2] != supported_major || frame[3] > maximum_supported_minor) {
        return ProtocolStatus::kUnsupportedVersion;
    }
    if (!IsValidMessageClass(frame[4])) {
        return ProtocolStatus::kInvalidMessageClass;
    }
    if ((frame[5] & ~kAllowedFrameFlags) != 0) {
        return ProtocolStatus::kInvalidFlags;
    }
    if (GetUInt16(frame + 18) != 0) {
        return ProtocolStatus::kInvalidReservedField;
    }
    const std::size_t payload_size = GetUInt16(frame + 16);
    if (payload_size > kMaximumPayloadSize) return ProtocolStatus::kOversized;
    const std::size_t expected_size = kFrameHeaderSize + payload_size;
    if (frame_size < expected_size) return ProtocolStatus::kTruncated;
    if (frame_size > expected_size) return ProtocolStatus::kOversized;

    uint32_t crc = Crc32(frame, 20);
    crc = Crc32(frame + kFrameHeaderSize, payload_size, crc);
    if (crc != GetUInt32(frame + 20)) return ProtocolStatus::kCrcMismatch;

    output->header.major = frame[2];
    output->header.minor = frame[3];
    output->header.message_class = static_cast<MessageClass>(frame[4]);
    output->header.flags = frame[5];
    output->header.message_type = GetUInt16(frame + 6);
    output->header.request_id = GetUInt32(frame + 8);
    output->header.sequence = GetUInt32(frame + 12);
    output->payload = frame + kFrameHeaderSize;
    output->payload_size = payload_size;
    return ProtocolStatus::kOk;
}

ProtocolStatus EncodeEnrollmentProofValue(
    uint32_t generation, const uint8_t token[16],
    const uint8_t companion_id[16], uint8_t* output,
    std::size_t output_capacity, std::size_t* output_size) {
    if (token == nullptr || companion_id == nullptr || output == nullptr ||
        output_size == nullptr) {
        return ProtocolStatus::kInvalidArgument;
    }
    *output_size = 0;
    constexpr std::size_t kEnrollmentProofValueSize = 4 + 16 + 16;
    if (output_capacity < kEnrollmentProofValueSize) {
        return ProtocolStatus::kBufferTooSmall;
    }
    PutUInt32(output, generation);
    std::memcpy(output + 4, token, 16);
    std::memcpy(output + 20, companion_id, 16);
    *output_size = kEnrollmentProofValueSize;
    return ProtocolStatus::kOk;
}

ProtocolStatus DecodeEnrollmentProofValue(const uint8_t* value,
                                          std::size_t value_size,
                                          uint32_t* generation,
                                          uint8_t* token,
                                          uint8_t* companion_id) {
    if (value == nullptr || generation == nullptr || token == nullptr ||
        companion_id == nullptr) {
        return ProtocolStatus::kInvalidArgument;
    }
    if (value_size != 36) return ProtocolStatus::kMalformedTlv;
    *generation = GetUInt32(value);
    std::memcpy(token, value + 4, 16);
    std::memcpy(companion_id, value + 20, 16);
    return ProtocolStatus::kOk;
}

ProtocolStatus EncodeCompanionIdentityValue(const uint8_t companion_id[16],
                                            uint8_t* output,
                                            std::size_t output_capacity,
                                            std::size_t* output_size) {
    if (companion_id == nullptr || output == nullptr ||
        output_size == nullptr) {
        return ProtocolStatus::kInvalidArgument;
    }
    *output_size = 0;
    if (output_capacity < 16) return ProtocolStatus::kBufferTooSmall;
    std::memcpy(output, companion_id, 16);
    *output_size = 16;
    return ProtocolStatus::kOk;
}

ProtocolStatus DecodeCompanionIdentityValue(const uint8_t* value,
                                            std::size_t value_size,
                                            uint8_t* companion_id) {
    if (value == nullptr || companion_id == nullptr) {
        return ProtocolStatus::kInvalidArgument;
    }
    if (value_size != 16) return ProtocolStatus::kMalformedTlv;
    std::memcpy(companion_id, value, 16);
    return ProtocolStatus::kOk;
}

ProtocolStatus EncodeHelloAckStatusValue(uint8_t status, uint8_t flags,
                                         uint16_t error_reason,
                                         uint8_t* output,
                                         std::size_t output_capacity,
                                         std::size_t* output_size) {
    if (output == nullptr || output_size == nullptr) {
        return ProtocolStatus::kInvalidArgument;
    }
    *output_size = 0;
    if (output_capacity < 4) return ProtocolStatus::kBufferTooSmall;
    output[0] = status;
    output[1] = flags;
    PutUInt16(output + 2, error_reason);
    *output_size = 4;
    return ProtocolStatus::kOk;
}

ProtocolStatus DecodeHelloAckStatusValue(const uint8_t* value,
                                         std::size_t value_size,
                                         uint8_t* status, uint8_t* flags,
                                         uint16_t* error_reason) {
    if (value == nullptr || status == nullptr || flags == nullptr ||
        error_reason == nullptr) {
        return ProtocolStatus::kInvalidArgument;
    }
    if (value_size != 4) return ProtocolStatus::kMalformedTlv;
    *status = value[0];
    *flags = value[1];
    *error_reason = GetUInt16(value + 2);
    return ProtocolStatus::kOk;
}

ProtocolStatus TlvWriter::Add(uint16_t type, const uint8_t* value,
                              std::size_t value_size) {
    if (output_ == nullptr || (value == nullptr && value_size != 0)) {
        return ProtocolStatus::kInvalidArgument;
    }
    if (value_size > kMaximumTlvValueSize) return ProtocolStatus::kOversized;
    if (capacity_ - std::min(capacity_, size_) < 4 + value_size) {
        return ProtocolStatus::kBufferTooSmall;
    }
    PutUInt16(output_ + size_, type);
    PutUInt16(output_ + size_ + 2, static_cast<uint16_t>(value_size));
    if (value_size != 0) std::memcpy(output_ + size_ + 4, value, value_size);
    size_ += 4 + value_size;
    return ProtocolStatus::kOk;
}

ProtocolStatus TlvWriter::AddUInt32(uint16_t type, uint32_t value) {
    uint8_t encoded[4];
    PutUInt32(encoded, value);
    return Add(type, encoded, sizeof(encoded));
}

ProtocolStatus TlvReader::Next(TlvField* field, bool* has_field) {
    if (field == nullptr || has_field == nullptr ||
        (input_ == nullptr && size_ != 0)) {
        return ProtocolStatus::kInvalidArgument;
    }
    *field = {};
    *has_field = false;
    if (offset_ == size_) return ProtocolStatus::kOk;
    if (size_ - offset_ < 4) return ProtocolStatus::kMalformedTlv;
    const uint16_t raw_type = GetUInt16(input_ + offset_);
    const std::size_t value_size = GetUInt16(input_ + offset_ + 2);
    if (value_size > kMaximumTlvValueSize ||
        value_size > size_ - offset_ - 4) {
        return ProtocolStatus::kMalformedTlv;
    }
    field->required = (raw_type & kRequiredFieldBit) != 0;
    field->type = raw_type & ~kRequiredFieldBit;
    field->value = input_ + offset_ + 4;
    field->value_size = value_size;
    offset_ += 4 + value_size;
    *has_field = true;
    return ProtocolStatus::kOk;
}

std::size_t FragmentCount(std::size_t frame_size,
                          std::size_t packet_capacity) {
    if (frame_size == 0 || frame_size > kMaximumFrameSize ||
        packet_capacity <= kFragmentHeaderSize) {
        return 0;
    }
    const std::size_t data_capacity = packet_capacity - kFragmentHeaderSize;
    return (frame_size + data_capacity - 1) / data_capacity;
}

ProtocolStatus EncodeFragment(const uint8_t* frame, std::size_t frame_size,
                              uint16_t frame_id, std::size_t fragment_index,
                              std::size_t packet_capacity, uint8_t* output,
                              std::size_t output_capacity,
                              std::size_t* output_size) {
    if (frame == nullptr || output == nullptr || output_size == nullptr) {
        return ProtocolStatus::kInvalidArgument;
    }
    *output_size = 0;
    const std::size_t count = FragmentCount(frame_size, packet_capacity);
    if (count == 0 || fragment_index >= count) {
        return ProtocolStatus::kInvalidArgument;
    }
    const std::size_t data_capacity = packet_capacity - kFragmentHeaderSize;
    const std::size_t offset = fragment_index * data_capacity;
    if (offset > UINT16_MAX) return ProtocolStatus::kOversized;
    const std::size_t data_size = std::min(data_capacity, frame_size - offset);
    const std::size_t packet_size = kFragmentHeaderSize + data_size;
    if (output_capacity < packet_size) return ProtocolStatus::kBufferTooSmall;

    output[0] = kFragmentMagic;
    output[1] = kFragmentVersion;
    output[2] = 0;
    if (fragment_index == 0) output[2] |= kFragmentStart;
    if (fragment_index + 1 == count) output[2] |= kFragmentEnd;
    output[3] = 0;
    PutUInt16(output + 4, frame_id);
    PutUInt16(output + 6, static_cast<uint16_t>(offset));
    std::memcpy(output + kFragmentHeaderSize, frame + offset, data_size);
    *output_size = packet_size;
    return ProtocolStatus::kOk;
}

ProtocolStatus FragmentReassembler::Accept(const uint8_t* packet,
                                           std::size_t packet_size) {
    if (packet == nullptr) return ProtocolStatus::kInvalidArgument;
    if (packet_size <= kFragmentHeaderSize) {
        return ProtocolStatus::kUnexpectedFragment;
    }
    if (packet[0] != kFragmentMagic || packet[1] != kFragmentVersion ||
        packet[3] != 0 || (packet[2] & ~(kFragmentStart | kFragmentEnd)) != 0) {
        return ProtocolStatus::kUnexpectedFragment;
    }
    const bool start = (packet[2] & kFragmentStart) != 0;
    const bool end = (packet[2] & kFragmentEnd) != 0;
    const uint16_t frame_id = GetUInt16(packet + 4);
    const std::size_t offset = GetUInt16(packet + 6);
    const std::size_t data_size = packet_size - kFragmentHeaderSize;

    if (start) {
        if (offset != 0) return ProtocolStatus::kOutOfOrderFragment;
        Reset();
        active_ = true;
        frame_id_ = frame_id;
    } else if (!active_ || frame_id != frame_id_) {
        return ProtocolStatus::kUnexpectedFragment;
    }
    if (offset != size_) return ProtocolStatus::kOutOfOrderFragment;
    if (data_size > buffer_.size() - size_) {
        Reset();
        return ProtocolStatus::kOversized;
    }
    std::memcpy(buffer_.data() + size_, packet + kFragmentHeaderSize,
                data_size);
    size_ += data_size;

    if (expected_size_ == 0 && size_ >= kFrameHeaderSize) {
        if (GetUInt16(buffer_.data()) != kFrameMagic) {
            Reset();
            return ProtocolStatus::kBadMagic;
        }
        const std::size_t payload_size = GetUInt16(buffer_.data() + 16);
        if (payload_size > kMaximumPayloadSize) {
            Reset();
            return ProtocolStatus::kOversized;
        }
        expected_size_ = kFrameHeaderSize + payload_size;
    }
    if (expected_size_ != 0 && size_ > expected_size_) {
        Reset();
        return ProtocolStatus::kOversized;
    }
    if (end) {
        if (expected_size_ == 0 || size_ != expected_size_) {
            Reset();
            return ProtocolStatus::kTruncated;
        }
        active_ = false;
        return ProtocolStatus::kFrameComplete;
    }
    if (expected_size_ != 0 && size_ == expected_size_) {
        Reset();
        return ProtocolStatus::kUnexpectedFragment;
    }
    return ProtocolStatus::kFragmentAccepted;
}

void FragmentReassembler::Reset() {
    size_ = 0;
    expected_size_ = 0;
    frame_id_ = 0;
    active_ = false;
}

}  // namespace zectrix::companion
