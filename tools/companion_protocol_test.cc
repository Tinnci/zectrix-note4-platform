#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include "zectrix_companion_protocol.h"

using zectrix::companion::DecodeFrame;
using zectrix::companion::EncodeFragment;
using zectrix::companion::EncodeFrame;
using zectrix::companion::FragmentCount;
using zectrix::companion::FragmentReassembler;
using zectrix::companion::FrameHeader;
using zectrix::companion::FrameView;
using zectrix::companion::MessageClass;
using zectrix::companion::ProtocolStatus;
using zectrix::companion::TlvField;
using zectrix::companion::TlvReader;
using zectrix::companion::TlvWriter;

namespace {

std::vector<uint8_t> MakeFrame() {
    std::array<uint8_t, 32> payload{};
    TlvWriter writer(payload.data(), payload.size());
    assert(writer.AddUInt32(0x8001, 0x12345678) == ProtocolStatus::kOk);
    const uint8_t text[] = {'o', 'k'};
    assert(writer.Add(2, text, sizeof(text)) == ProtocolStatus::kOk);

    FrameHeader header{};
    header.message_class = MessageClass::kCommand;
    header.flags = zectrix::companion::kAckRequested |
                   zectrix::companion::kRetriable;
    header.message_type = 0x0102;
    header.request_id = 0x11223344;
    header.sequence = 0x55667788;
    std::vector<uint8_t> frame(zectrix::companion::kMaximumFrameSize);
    std::size_t frame_size = 0;
    assert(EncodeFrame(header, payload.data(), writer.Size(), frame.data(),
                       frame.size(), &frame_size) == ProtocolStatus::kOk);
    frame.resize(frame_size);
    return frame;
}

void TestCrcAndFrame() {
    const uint8_t crc_input[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    assert(zectrix::companion::Crc32(crc_input, sizeof(crc_input)) ==
           0xcbf43926U);

    const auto frame = MakeFrame();
    const uint8_t golden[] = {
        0x5a, 0x43, 0x01, 0x00, 0x02, 0x05, 0x02, 0x01,
        0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55,
        0x0e, 0x00, 0x00, 0x00, 0x5f, 0xae, 0xbb, 0x01,
        0x01, 0x80, 0x04, 0x00, 0x78, 0x56, 0x34, 0x12,
        0x02, 0x00, 0x02, 0x00, 0x6f, 0x6b,
    };
    assert(frame.size() == sizeof(golden));
    assert(std::memcmp(frame.data(), golden, sizeof(golden)) == 0);
    FrameView view{};
    assert(DecodeFrame(frame.data(), frame.size(), 1, 0, &view) ==
           ProtocolStatus::kOk);
    assert(view.header.message_class == MessageClass::kCommand);
    assert(view.header.message_type == 0x0102);
    assert(view.header.request_id == 0x11223344);
    assert(view.header.sequence == 0x55667788);
    assert(view.payload_size == 14);

    TlvReader reader(view.payload, view.payload_size);
    TlvField field{};
    bool present = false;
    assert(reader.Next(&field, &present) == ProtocolStatus::kOk && present);
    assert(field.required && field.type == 1 && field.value_size == 4);
    assert(reader.Next(&field, &present) == ProtocolStatus::kOk && present);
    assert(!field.required && field.type == 2 && field.value_size == 2);
    assert(reader.Next(&field, &present) == ProtocolStatus::kOk && !present);

    for (std::size_t index = 0; index < frame.size(); ++index) {
        auto corrupt = frame;
        corrupt[index] ^= 0x01;
        const auto status = DecodeFrame(corrupt.data(), corrupt.size(), 1, 0,
                                        &view);
        assert(status != ProtocolStatus::kOk);
    }

    assert(DecodeFrame(frame.data(), frame.size() - 1, 1, 0, &view) ==
           ProtocolStatus::kTruncated);
    std::vector<uint8_t> extra = frame;
    extra.push_back(0);
    assert(DecodeFrame(extra.data(), extra.size(), 1, 0, &view) ==
           ProtocolStatus::kOversized);
    assert(DecodeFrame(frame.data(), frame.size(), 2, 0, &view) ==
           ProtocolStatus::kUnsupportedVersion);
}

void TestBoundsAndTlvErrors() {
    std::array<uint8_t, 8> output{};
    TlvWriter writer(output.data(), output.size());
    const uint8_t value[5]{};
    assert(writer.Add(1, value, sizeof(value)) ==
           ProtocolStatus::kBufferTooSmall);

    const uint8_t truncated[] = {1, 0, 4, 0, 1, 2};
    TlvReader reader(truncated, sizeof(truncated));
    TlvField field{};
    bool present = false;
    assert(reader.Next(&field, &present) == ProtocolStatus::kMalformedTlv);

    std::array<uint8_t, zectrix::companion::kMaximumPayloadSize + 1> payload{};
    std::array<uint8_t, zectrix::companion::kMaximumFrameSize + 1> frame{};
    std::size_t size = 0;
    assert(EncodeFrame({}, payload.data(), payload.size(), frame.data(),
                       frame.size(), &size) == ProtocolStatus::kOversized);
}

void TestFragmentation() {
    const auto frame = MakeFrame();
    constexpr std::size_t packet_capacity = 23;
    const std::size_t count = FragmentCount(frame.size(), packet_capacity);
    assert(count == 3);

    FragmentReassembler reassembler;
    std::array<uint8_t, packet_capacity> packet{};
    for (std::size_t index = 0; index < count; ++index) {
        std::size_t packet_size = 0;
        assert(EncodeFragment(frame.data(), frame.size(), 42, index,
                              packet_capacity, packet.data(), packet.size(),
                              &packet_size) == ProtocolStatus::kOk);
        const ProtocolStatus expected = index + 1 == count
                                            ? ProtocolStatus::kFrameComplete
                                            : ProtocolStatus::kFragmentAccepted;
        assert(reassembler.Accept(packet.data(), packet_size) == expected);
    }
    assert(reassembler.FrameId() == 42);
    assert(reassembler.Size() == frame.size());
    assert(std::memcmp(reassembler.Data(), frame.data(), frame.size()) == 0);

    std::size_t first_size = 0;
    std::size_t last_size = 0;
    std::array<uint8_t, packet_capacity> first{};
    std::array<uint8_t, packet_capacity> last{};
    assert(EncodeFragment(frame.data(), frame.size(), 7, 0, packet_capacity,
                          first.data(), first.size(), &first_size) ==
           ProtocolStatus::kOk);
    assert(EncodeFragment(frame.data(), frame.size(), 7, count - 1,
                          packet_capacity, last.data(), last.size(),
                          &last_size) == ProtocolStatus::kOk);
    reassembler.Reset();
    assert(reassembler.Accept(first.data(), first_size) ==
           ProtocolStatus::kFragmentAccepted);
    assert(reassembler.Accept(last.data(), last_size) ==
           ProtocolStatus::kOutOfOrderFragment);

    reassembler.Reset();
    assert(reassembler.Accept(last.data(), last_size) ==
           ProtocolStatus::kUnexpectedFragment);
}

}  // namespace

int main() {
    TestCrcAndFrame();
    TestBoundsAndTlvErrors();
    TestFragmentation();
    return 0;
}
