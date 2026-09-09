#include "zectrix_update_service.h"

#include <algorithm>
#include <cstring>

namespace zectrix::update {

uint32_t FirmwareCrc32(const uint8_t* data, std::size_t size, uint32_t previous) {
    uint32_t crc = ~previous;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
        }
    }
    return ~crc;
}

UpdateService::~UpdateService() { AbortFirmware(); }

Result UpdateService::BeginFirmware(uint32_t image_bytes, uint32_t image_crc32) {
    if (firmware_.phase == FirmwarePhase::kReceiving ||
        firmware_.phase == FirmwarePhase::kCommitted) return Result::kInvalidState;
    if (image_bytes <= kFirmwareHeaderBytes) return Result::kInvalidArgument;
    Partition target;
    const auto result = SelectUpdateTarget(image_bytes, &target);
    if (result != Result::kOk) return result;
    firmware_ = {target, image_bytes, 0, FirmwarePhase::kReceiving};
    header_bytes_ = 0;
    expected_crc32_ = image_crc32;
    received_crc32_ = 0;
    return Result::kOk;
}

Result UpdateService::WriteFirmwareChunk(uint32_t offset, const uint8_t* data,
                                         std::size_t size, uint32_t chunk_crc32) {
    if (firmware_.phase != FirmwarePhase::kReceiving) return Result::kInvalidState;
    if (data == nullptr || size == 0 || size > kMaximumFirmwareChunkBytes ||
        size > firmware_.image_bytes - firmware_.received_bytes) return Result::kInvalidArgument;
    if (offset != firmware_.received_bytes) return Result::kUnexpectedOffset;
    if (FirmwareCrc32(data, size) != chunk_crc32) return Result::kChecksumMismatch;

    const auto copied = std::min(size, header_.size() - header_bytes_);
    if (copied != 0) {
        std::memcpy(header_.data() + header_bytes_, data, copied);
        header_bytes_ += copied;
    }
    if (!image_open_ && header_bytes_ == header_.size()) {
        // No erase or write occurs until the full prefix is available and valid.
        Partition target;
        auto result = SelectUpdateTarget(firmware_.image_bytes, &target);
        if (result != Result::kOk) return FailFirmware(result);
        if (!SamePartition(target, firmware_.target)) return FailFirmware(Result::kInvalidLayout);
        result = backend_.BeginImage(target, firmware_.image_bytes, header_.data(), header_.size());
        if (result != Result::kOk) return FailFirmware(result);
        image_open_ = true;
        result = backend_.WriteImage(header_.data(), header_.size());
        if (result != Result::kOk) return FailFirmware(result);
    }
    if (image_open_ && copied < size) {
        const auto result = backend_.WriteImage(data + copied, size - copied);
        if (result != Result::kOk) return FailFirmware(result);
    }
    received_crc32_ = FirmwareCrc32(data, size, received_crc32_);
    firmware_.received_bytes += static_cast<uint32_t>(size);
    return Result::kOk;
}

Result UpdateService::CommitFirmware() {
    if (firmware_.phase != FirmwarePhase::kReceiving) return Result::kInvalidState;
    if (firmware_.received_bytes != firmware_.image_bytes) return Result::kIncompleteImage;
    if (received_crc32_ != expected_crc32_) return FailFirmware(Result::kChecksumMismatch);
    Partition target;
    auto result = SelectUpdateTarget(firmware_.image_bytes, &target);
    if (result != Result::kOk) return FailFirmware(result);
    if (!SamePartition(target, firmware_.target)) return FailFirmware(Result::kInvalidLayout);
    result = backend_.CommitImage(expected_crc32_);
    if (result != Result::kOk) return FailFirmware(result);
    image_open_ = false;
    firmware_.phase = FirmwarePhase::kCommitted;
    boot_.status_.boot = firmware_.target;
    boot_.status_.layout_result = Result::kBootMismatch;
    return Result::kOk;
}

Result UpdateService::FailFirmware(Result result) {
    // A flash operation can fail after a partial write. Restart from offset zero.
    backend_.AbortImage();
    image_open_ = false;
    firmware_.phase = FirmwarePhase::kFailed;
    return result;
}

void UpdateService::AbortFirmware() {
    if (image_open_) backend_.AbortImage();
    image_open_ = false;
    // Aborting after commit must not silently cancel an already selected boot.
    if (firmware_.phase != FirmwarePhase::kCommitted) firmware_ = {};
}

}  // namespace zectrix::update
