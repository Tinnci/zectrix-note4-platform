#include "zectrix_resource_gateway.h"

#include <array>

namespace zectrix::companion {
namespace {

constexpr uint16_t kRequestCapabilityType = 1;
constexpr uint16_t kRequestMaximumBytesType = 2;
constexpr uint16_t kRequestTimeoutMsType = 3;
constexpr uint16_t kRequestCacheMaxAgeSecondsType = 4;
constexpr uint16_t kRequestDurableType = 5;

constexpr uint16_t kResponseStatusType = 1;
constexpr uint16_t kResponseContentTypeType = 2;
constexpr uint16_t kResponseBodyType = 3;
constexpr uint16_t kResponseRetryAfterMsType = 4;

uint16_t GetUInt16(const uint8_t* value) {
    return static_cast<uint16_t>(value[0]) |
           static_cast<uint16_t>(value[1]) << 8U;
}

uint32_t GetUInt32(const uint8_t* value) {
    return static_cast<uint32_t>(value[0]) |
           static_cast<uint32_t>(value[1]) << 8U |
           static_cast<uint32_t>(value[2]) << 16U |
           static_cast<uint32_t>(value[3]) << 24U;
}

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

bool IsValidStatus(uint8_t value) {
    return value <=
        static_cast<uint8_t>(ResourceStatus::kUnsupportedCapability);
}

bool IsValidContentType(uint8_t value) {
    return value <= static_cast<uint8_t>(ResourceContentType::kTextPlainUtf8);
}

}  // namespace

ProtocolStatus EncodeResourceRequestPayload(
    const ResourceRequestMessage& request, uint8_t* output,
    std::size_t output_capacity, std::size_t* output_size) {
    if (output == nullptr || output_size == nullptr) {
        return ProtocolStatus::kInvalidArgument;
    }
    *output_size = 0;
    if (request.maximum_response_bytes == 0 ||
        request.maximum_response_bytes > kResourceMaximumBodySize ||
        request.timeout_ms < kResourceMinimumTimeoutMs ||
        request.timeout_ms > kResourceMaximumTimeoutMs) {
        return ProtocolStatus::kInvalidArgument;
    }

    TlvWriter writer(output, output_capacity);
    std::array<uint8_t, 4> value{};
    PutUInt16(value.data(), static_cast<uint16_t>(request.capability));
    ProtocolStatus status = writer.Add(
        kRequestCapabilityType | kRequiredFieldBit, value.data(), 2);
    if (status != ProtocolStatus::kOk) return status;
    PutUInt16(value.data(), request.maximum_response_bytes);
    status = writer.Add(kRequestMaximumBytesType | kRequiredFieldBit,
                        value.data(), 2);
    if (status != ProtocolStatus::kOk) return status;
    PutUInt32(value.data(), request.timeout_ms);
    status = writer.Add(kRequestTimeoutMsType | kRequiredFieldBit,
                        value.data(), 4);
    if (status != ProtocolStatus::kOk) return status;
    PutUInt32(value.data(), request.cache_max_age_seconds);
    status = writer.Add(kRequestCacheMaxAgeSecondsType, value.data(), 4);
    if (status != ProtocolStatus::kOk) return status;
    value[0] = request.durable ? 1 : 0;
    status = writer.Add(kRequestDurableType | kRequiredFieldBit,
                        value.data(), 1);
    if (status != ProtocolStatus::kOk) return status;
    *output_size = writer.Size();
    return ProtocolStatus::kOk;
}

ProtocolStatus DecodeResourceRequestPayload(
    const uint8_t* payload, std::size_t payload_size,
    ResourceRequestMessage* request) {
    if ((payload == nullptr && payload_size != 0) || request == nullptr) {
        return ProtocolStatus::kInvalidArgument;
    }
    *request = {};
    bool capability_seen = false;
    bool maximum_seen = false;
    bool timeout_seen = false;
    bool cache_seen = false;
    bool durable_seen = false;
    TlvReader reader(payload, payload_size);
    TlvField field{};
    bool present = false;
    while (true) {
        ProtocolStatus status = reader.Next(&field, &present);
        if (status != ProtocolStatus::kOk) return status;
        if (!present) break;
        switch (field.type) {
            case kRequestCapabilityType:
                if (capability_seen || field.value_size != 2) {
                    return ProtocolStatus::kMalformedTlv;
                }
                capability_seen = true;
                request->capability =
                    static_cast<ResourceCapability>(GetUInt16(field.value));
                break;
            case kRequestMaximumBytesType:
                if (maximum_seen || field.value_size != 2) {
                    return ProtocolStatus::kMalformedTlv;
                }
                maximum_seen = true;
                request->maximum_response_bytes = GetUInt16(field.value);
                break;
            case kRequestTimeoutMsType:
                if (timeout_seen || field.value_size != 4) {
                    return ProtocolStatus::kMalformedTlv;
                }
                timeout_seen = true;
                request->timeout_ms = GetUInt32(field.value);
                break;
            case kRequestCacheMaxAgeSecondsType:
                if (cache_seen || field.value_size != 4) {
                    return ProtocolStatus::kMalformedTlv;
                }
                cache_seen = true;
                request->cache_max_age_seconds = GetUInt32(field.value);
                break;
            case kRequestDurableType:
                if (durable_seen || field.value_size != 1 ||
                    field.value[0] > 1) {
                    return ProtocolStatus::kMalformedTlv;
                }
                durable_seen = true;
                request->durable = field.value[0] != 0;
                break;
            default:
                if (field.required) return ProtocolStatus::kMalformedTlv;
                break;
        }
    }
    if (!capability_seen || !maximum_seen || !timeout_seen || !durable_seen ||
        request->maximum_response_bytes == 0 ||
        request->maximum_response_bytes > kResourceMaximumBodySize ||
        request->timeout_ms < kResourceMinimumTimeoutMs ||
        request->timeout_ms > kResourceMaximumTimeoutMs) {
        return ProtocolStatus::kMalformedTlv;
    }
    return ProtocolStatus::kOk;
}

ProtocolStatus EncodeResourceResponsePayload(
    const ResourceResponseMessage& response, uint8_t* output,
    std::size_t output_capacity, std::size_t* output_size) {
    if (output == nullptr || output_size == nullptr ||
        (response.body == nullptr && response.body_size != 0) ||
        response.body_size > kResourceMaximumBodySize) {
        return ProtocolStatus::kInvalidArgument;
    }
    *output_size = 0;
    const bool success = response.status == ResourceStatus::kSuccess;
    if ((success && (response.content_type == ResourceContentType::kNone ||
                     response.body == nullptr || response.body_size == 0)) ||
        (!success && (response.content_type != ResourceContentType::kNone ||
                      response.body_size != 0))) {
        return ProtocolStatus::kInvalidArgument;
    }

    TlvWriter writer(output, output_capacity);
    uint8_t value[4] = {static_cast<uint8_t>(response.status), 0, 0, 0};
    ProtocolStatus status = writer.Add(
        kResponseStatusType | kRequiredFieldBit, value, 1);
    if (status != ProtocolStatus::kOk) return status;
    if (success) {
        value[0] = static_cast<uint8_t>(response.content_type);
        status = writer.Add(kResponseContentTypeType | kRequiredFieldBit,
                            value, 1);
        if (status != ProtocolStatus::kOk) return status;
        status = writer.Add(kResponseBodyType | kRequiredFieldBit,
                            response.body, response.body_size);
        if (status != ProtocolStatus::kOk) return status;
    }
    if (response.retry_after_ms != 0) {
        PutUInt32(value, response.retry_after_ms);
        status = writer.Add(kResponseRetryAfterMsType, value, 4);
        if (status != ProtocolStatus::kOk) return status;
    }
    *output_size = writer.Size();
    return ProtocolStatus::kOk;
}

ProtocolStatus DecodeResourceResponsePayload(
    const uint8_t* payload, std::size_t payload_size,
    ResourceResponseMessage* response) {
    if ((payload == nullptr && payload_size != 0) || response == nullptr) {
        return ProtocolStatus::kInvalidArgument;
    }
    *response = {};
    bool status_seen = false;
    bool content_seen = false;
    bool body_seen = false;
    bool retry_seen = false;
    TlvReader reader(payload, payload_size);
    TlvField field{};
    bool present = false;
    while (true) {
        ProtocolStatus status = reader.Next(&field, &present);
        if (status != ProtocolStatus::kOk) return status;
        if (!present) break;
        switch (field.type) {
            case kResponseStatusType:
                if (status_seen || field.value_size != 1 ||
                    !IsValidStatus(field.value[0])) {
                    return ProtocolStatus::kMalformedTlv;
                }
                status_seen = true;
                response->status =
                    static_cast<ResourceStatus>(field.value[0]);
                break;
            case kResponseContentTypeType:
                if (content_seen || field.value_size != 1 ||
                    !IsValidContentType(field.value[0])) {
                    return ProtocolStatus::kMalformedTlv;
                }
                content_seen = true;
                response->content_type =
                    static_cast<ResourceContentType>(field.value[0]);
                break;
            case kResponseBodyType:
                if (body_seen || field.value_size == 0 ||
                    field.value_size > kResourceMaximumBodySize) {
                    return ProtocolStatus::kMalformedTlv;
                }
                body_seen = true;
                response->body = field.value;
                response->body_size = field.value_size;
                break;
            case kResponseRetryAfterMsType:
                if (retry_seen || field.value_size != 4) {
                    return ProtocolStatus::kMalformedTlv;
                }
                retry_seen = true;
                response->retry_after_ms = GetUInt32(field.value);
                break;
            default:
                if (field.required) return ProtocolStatus::kMalformedTlv;
                break;
        }
    }
    if (!status_seen) return ProtocolStatus::kMalformedTlv;
    const bool success = response->status == ResourceStatus::kSuccess;
    if ((success && (!content_seen || !body_seen ||
                     response->content_type == ResourceContentType::kNone)) ||
        (!success && (content_seen || body_seen))) {
        return ProtocolStatus::kMalformedTlv;
    }
    return ProtocolStatus::kOk;
}

bool IsTransientResourceStatus(ResourceStatus status) {
    return status == ResourceStatus::kPhoneUnavailable ||
           status == ResourceStatus::kPhoneOffline ||
           status == ResourceStatus::kTimeout;
}

}  // namespace zectrix::companion
