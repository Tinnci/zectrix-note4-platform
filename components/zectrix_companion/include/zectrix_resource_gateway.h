#pragma once

#include <cstddef>
#include <cstdint>

#include "zectrix_connectivity_policy.h"
#include "zectrix_companion_protocol.h"

namespace zectrix::companion {

constexpr uint16_t kResourceRequestMessageType = 0x0100;
constexpr std::size_t kResourceMaximumBodySize = 2048;
constexpr uint32_t kResourceMinimumTimeoutMs = 1000;
constexpr uint32_t kResourceMaximumTimeoutMs = 15000;

enum class ResourceStatus : uint8_t {
    kSuccess = 0,
    kPhoneUnavailable = 1,
    kPhoneOffline = 2,
    kTimeout = 3,
    kServerError = 4,
    kResponseTooLarge = 5,
    kInvalidResponse = 6,
    kNotAuthorized = 7,
    kUnsupportedCapability = 8,
};

enum class ResourceContentType : uint8_t {
    kNone = 0,
    kTextPlainUtf8 = 1,
};

struct ResourceRequestMessage {
    ResourceCapability capability = ResourceCapability::kPublicTestDocumentV1;
    uint16_t maximum_response_bytes =
        static_cast<uint16_t>(kResourceMaximumBodySize);
    uint32_t timeout_ms = 10000;
    uint32_t cache_max_age_seconds = 0;
    bool durable = true;
};

struct ResourceResponseMessage {
    ResourceStatus status = ResourceStatus::kInvalidResponse;
    ResourceContentType content_type = ResourceContentType::kNone;
    const uint8_t* body = nullptr;
    std::size_t body_size = 0;
    uint32_t retry_after_ms = 0;
};

ProtocolStatus EncodeResourceRequestPayload(
    const ResourceRequestMessage& request, uint8_t* output,
    std::size_t output_capacity, std::size_t* output_size);

ProtocolStatus DecodeResourceRequestPayload(
    const uint8_t* payload, std::size_t payload_size,
    ResourceRequestMessage* request);

ProtocolStatus EncodeResourceResponsePayload(
    const ResourceResponseMessage& response, uint8_t* output,
    std::size_t output_capacity, std::size_t* output_size);

ProtocolStatus DecodeResourceResponsePayload(
    const uint8_t* payload, std::size_t payload_size,
    ResourceResponseMessage* response);

bool IsTransientResourceStatus(ResourceStatus status);

}  // namespace zectrix::companion
