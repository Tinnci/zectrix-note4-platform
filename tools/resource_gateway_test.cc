#include <array>
#include <cassert>
#include <cstring>

#include "zectrix_resource_gateway.h"

using namespace zectrix::companion;

namespace {

void TestRequestRoundTripAndBounds() {
    ResourceRequestMessage request{};
    request.maximum_response_bytes = 2048;
    request.timeout_ms = 10000;
    request.cache_max_age_seconds = 0;
    request.durable = true;
    std::array<uint8_t, 64> payload{};
    std::size_t payload_size = 0;
    assert(EncodeResourceRequestPayload(request, payload.data(),
                                        payload.size(), &payload_size) ==
           ProtocolStatus::kOk);
    const uint8_t expected[] = {
        0x01, 0x80, 0x02, 0x00, 0x01, 0x00,
        0x02, 0x80, 0x02, 0x00, 0x00, 0x08,
        0x03, 0x80, 0x04, 0x00, 0x10, 0x27, 0x00, 0x00,
        0x04, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x05, 0x80, 0x01, 0x00, 0x01,
    };
    assert(payload_size == sizeof(expected));
    assert(std::memcmp(payload.data(), expected, sizeof(expected)) == 0);

    FrameHeader header{};
    header.message_class = MessageClass::kCommand;
    header.flags = kAckRequested | kRetriable;
    header.message_type = kResourceRequestMessageType;
    header.request_id = 0x11223344;
    header.sequence = 1;
    std::array<uint8_t, kMaximumFrameSize> frame{};
    std::size_t frame_size = 0;
    assert(EncodeFrame(header, payload.data(), payload_size, frame.data(),
                       frame.size(), &frame_size) == ProtocolStatus::kOk);
    const uint8_t expected_frame[] = {
        0x5a, 0x43, 0x01, 0x00, 0x02, 0x05, 0x00, 0x01,
        0x44, 0x33, 0x22, 0x11, 0x01, 0x00, 0x00, 0x00,
        0x21, 0x00, 0x00, 0x00, 0xdc, 0x60, 0x17, 0x2c,
        0x01, 0x80, 0x02, 0x00, 0x01, 0x00,
        0x02, 0x80, 0x02, 0x00, 0x00, 0x08,
        0x03, 0x80, 0x04, 0x00, 0x10, 0x27, 0x00, 0x00,
        0x04, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x05, 0x80, 0x01, 0x00, 0x01,
    };
    assert(frame_size == sizeof(expected_frame));
    assert(std::memcmp(frame.data(), expected_frame,
                       sizeof(expected_frame)) == 0);

    ResourceRequestMessage decoded{};
    assert(DecodeResourceRequestPayload(payload.data(), payload_size,
                                        &decoded) == ProtocolStatus::kOk);
    assert(decoded.capability == ResourceCapability::kPublicTestDocumentV1);
    assert(decoded.maximum_response_bytes == 2048);
    assert(decoded.timeout_ms == 10000);
    assert(decoded.cache_max_age_seconds == 0);
    assert(decoded.durable);

    request.maximum_response_bytes = 2049;
    assert(EncodeResourceRequestPayload(request, payload.data(),
                                        payload.size(), &payload_size) ==
           ProtocolStatus::kInvalidArgument);
    request.maximum_response_bytes = 2048;
    request.timeout_ms = 999;
    assert(EncodeResourceRequestPayload(request, payload.data(),
                                        payload.size(), &payload_size) ==
           ProtocolStatus::kInvalidArgument);

    auto duplicate = std::array<uint8_t, 70>{};
    std::memcpy(duplicate.data(), expected, sizeof(expected));
    std::memcpy(duplicate.data() + sizeof(expected), expected, 6);
    assert(DecodeResourceRequestPayload(
               duplicate.data(), sizeof(expected) + 6, &decoded) ==
           ProtocolStatus::kMalformedTlv);
}

void TestResponseRoundTripAndSemantics() {
    const uint8_t body[] = {'o', 'k', '\n'};
    ResourceResponseMessage response{};
    response.status = ResourceStatus::kSuccess;
    response.content_type = ResourceContentType::kTextPlainUtf8;
    response.body = body;
    response.body_size = sizeof(body);
    std::array<uint8_t, 64> payload{};
    std::size_t payload_size = 0;
    assert(EncodeResourceResponsePayload(response, payload.data(),
                                         payload.size(), &payload_size) ==
           ProtocolStatus::kOk);
    const uint8_t expected[] = {
        0x01, 0x80, 0x01, 0x00, 0x00,
        0x02, 0x80, 0x01, 0x00, 0x01,
        0x03, 0x80, 0x03, 0x00, 0x6f, 0x6b, 0x0a,
    };
    assert(payload_size == sizeof(expected));
    assert(std::memcmp(payload.data(), expected, sizeof(expected)) == 0);

    ResourceResponseMessage decoded{};
    assert(DecodeResourceResponsePayload(payload.data(), payload_size,
                                         &decoded) == ProtocolStatus::kOk);
    assert(decoded.status == ResourceStatus::kSuccess);
    assert(decoded.content_type == ResourceContentType::kTextPlainUtf8);
    assert(decoded.body_size == sizeof(body));
    assert(std::memcmp(decoded.body, body, sizeof(body)) == 0);

    response = {};
    response.status = ResourceStatus::kPhoneOffline;
    response.retry_after_ms = 30000;
    assert(EncodeResourceResponsePayload(response, payload.data(),
                                         payload.size(), &payload_size) ==
           ProtocolStatus::kOk);
    assert(DecodeResourceResponsePayload(payload.data(), payload_size,
                                         &decoded) == ProtocolStatus::kOk);
    assert(decoded.status == ResourceStatus::kPhoneOffline);
    assert(decoded.retry_after_ms == 30000);
    assert(IsTransientResourceStatus(decoded.status));
    assert(!IsTransientResourceStatus(ResourceStatus::kServerError));

    response.body = body;
    response.body_size = sizeof(body);
    assert(EncodeResourceResponsePayload(response, payload.data(),
                                         payload.size(), &payload_size) ==
           ProtocolStatus::kInvalidArgument);
}

}  // namespace

int main() {
    static_assert(kResourceRequestMessageType == 0x0100);
    TestRequestRoundTripAndBounds();
    TestResponseRoundTripAndSemantics();
    return 0;
}
