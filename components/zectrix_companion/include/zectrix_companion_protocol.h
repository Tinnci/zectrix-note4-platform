#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace zectrix::companion {

constexpr uint16_t kFrameMagic = 0x435a;
constexpr uint8_t kProtocolMajor = 1;
constexpr uint8_t kProtocolMinor = 0;
constexpr std::size_t kFrameHeaderSize = 24;
constexpr std::size_t kMaximumPayloadSize = 4096;
constexpr std::size_t kMaximumFrameSize =
    kFrameHeaderSize + kMaximumPayloadSize;
constexpr std::size_t kMaximumTlvValueSize = 2048;
constexpr std::size_t kFragmentHeaderSize = 8;
constexpr uint16_t kRequiredFieldBit = 0x8000;

// Hello TLV types for vertical-slice sessions. A peer ignores an unknown TLV;
// an enrollment proof is optional in Hello v1.
constexpr uint16_t kHelloEnrollmentProofType = 1;
constexpr uint16_t kHelloCompanionIdentityType = 2;
constexpr uint16_t kHelloAckStatusType = 3;

constexpr uint8_t kHelloAckStatusOk = 0;
constexpr uint8_t kHelloAckStatusRejected = 1;
constexpr uint8_t kHelloAckPeerAuthorizedFlag = 1U << 0;
constexpr std::size_t kHelloAckStatusValueSize = 4;

enum class MessageClass : uint8_t {
    kControl = 0,
    kDurableState = 1,
    kCommand = 2,
    kStream = 3,
};

// Control messages that establish a protocol session. A BLE subscription is
// only transport readiness; Hello/HelloAck proves an authenticated write and
// a compatible protocol peer in both directions.
enum class ControlMessage : uint16_t {
    kHello = 1,
    kHelloAck = 2,
};

enum FrameFlag : uint8_t {
    kAckRequested = 1U << 0,
    kResponse = 1U << 1,
    kRetriable = 1U << 2,
};

enum class ProtocolStatus : uint8_t {
    kOk = 0,
    kInvalidArgument,
    kBufferTooSmall,
    kBadMagic,
    kUnsupportedVersion,
    kInvalidMessageClass,
    kInvalidFlags,
    kInvalidReservedField,
    kOversized,
    kTruncated,
    kCrcMismatch,
    kMalformedTlv,
    kUnexpectedFragment,
    kOutOfOrderFragment,
    kFragmentAccepted,
    kFrameComplete,
};

struct FrameHeader {
    uint8_t major = kProtocolMajor;
    uint8_t minor = kProtocolMinor;
    MessageClass message_class = MessageClass::kControl;
    uint8_t flags = 0;
    uint16_t message_type = 0;
    uint32_t request_id = 0;
    uint32_t sequence = 0;
};

struct FrameView {
    FrameHeader header{};
    const uint8_t* payload = nullptr;
    std::size_t payload_size = 0;
};

uint32_t Crc32(const uint8_t* data, std::size_t size,
               uint32_t previous = 0);

ProtocolStatus EncodeFrame(const FrameHeader& header, const uint8_t* payload,
                           std::size_t payload_size, uint8_t* output,
                           std::size_t output_capacity,
                           std::size_t* output_size);

ProtocolStatus DecodeFrame(const uint8_t* frame, std::size_t frame_size,
                           uint8_t supported_major,
                           uint8_t maximum_supported_minor,
                           FrameView* output);

class TlvWriter {
public:
    TlvWriter(uint8_t* output, std::size_t capacity)
        : output_(output), capacity_(capacity) {}

    ProtocolStatus Add(uint16_t type, const uint8_t* value,
                       std::size_t value_size);
    ProtocolStatus AddUInt32(uint16_t type, uint32_t value);
    std::size_t Size() const { return size_; }

private:
    uint8_t* output_ = nullptr;
    std::size_t capacity_ = 0;
    std::size_t size_ = 0;
};

struct TlvField {
    uint16_t type = 0;
    bool required = false;
    const uint8_t* value = nullptr;
    std::size_t value_size = 0;
};

// Encodes the Hello enrollment proof TLV value: generation uint32 LE,
// the 16-byte enrollment token, then the 16-byte companion identity that
// Android will present again on reconnect.
ProtocolStatus EncodeEnrollmentProofValue(
    uint32_t generation, const uint8_t token[16],
    const uint8_t companion_id[16], uint8_t* output,
    std::size_t output_capacity, std::size_t* output_size);

// Decodes a Hello enrollment proof TLV value.
ProtocolStatus DecodeEnrollmentProofValue(const uint8_t* value,
                                          std::size_t value_size,
                                          uint32_t* generation,
                                          uint8_t* token,
                                          uint8_t* companion_id);

// Encodes/decodes the reconnect-only Hello companion identity TLV value.
ProtocolStatus EncodeCompanionIdentityValue(const uint8_t companion_id[16],
                                            uint8_t* output,
                                            std::size_t output_capacity,
                                            std::size_t* output_size);
ProtocolStatus DecodeCompanionIdentityValue(const uint8_t* value,
                                            std::size_t value_size,
                                            uint8_t* companion_id);

// Encodes/decodes the HelloAck status TLV value: status byte, flags byte,
// and uint16 LE error reason.
ProtocolStatus EncodeHelloAckStatusValue(uint8_t status, uint8_t flags,
                                         uint16_t error_reason,
                                         uint8_t* output,
                                         std::size_t output_capacity,
                                         std::size_t* output_size);
ProtocolStatus DecodeHelloAckStatusValue(const uint8_t* value,
                                         std::size_t value_size,
                                         uint8_t* status, uint8_t* flags,
                                         uint16_t* error_reason);

class TlvReader {
public:
    TlvReader(const uint8_t* input, std::size_t size)
        : input_(input), size_(size) {}

    ProtocolStatus Next(TlvField* field, bool* has_field);

private:
    const uint8_t* input_ = nullptr;
    std::size_t size_ = 0;
    std::size_t offset_ = 0;
};

std::size_t FragmentCount(std::size_t frame_size,
                          std::size_t packet_capacity);

ProtocolStatus EncodeFragment(const uint8_t* frame, std::size_t frame_size,
                              uint16_t frame_id, std::size_t fragment_index,
                              std::size_t packet_capacity, uint8_t* output,
                              std::size_t output_capacity,
                              std::size_t* output_size);

class FragmentReassembler {
public:
    ProtocolStatus Accept(const uint8_t* packet, std::size_t packet_size);
    const uint8_t* Data() const { return buffer_.data(); }
    std::size_t Size() const { return size_; }
    uint16_t FrameId() const { return frame_id_; }
    void Reset();

private:
    std::array<uint8_t, kMaximumFrameSize> buffer_{};
    std::size_t size_ = 0;
    std::size_t expected_size_ = 0;
    uint16_t frame_id_ = 0;
    bool active_ = false;
};

}  // namespace zectrix::companion
