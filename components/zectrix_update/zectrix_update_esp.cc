#include "zectrix_update_esp.h"

#include <algorithm>
#include <cstring>

#include "bootloader_common.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

namespace zectrix::update {
namespace {

Partition Copy(const esp_partition_t* source) {
    Partition result;
    if (source == nullptr) return result;
    result.address = source->address;
    result.size = source->size;
    result.readonly = source->readonly;
    if (source->type == ESP_PARTITION_TYPE_APP) {
        if (source->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) result.kind = PartitionKind::kFactory;
        else if (source->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) result.kind = PartitionKind::kOtaA;
        else if (source->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) result.kind = PartitionKind::kOtaB;
        else if (source->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN &&
                 source->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_MAX) result.kind = PartitionKind::kOtherOta;
    } else if (source->type == ESP_PARTITION_TYPE_DATA &&
               source->subtype == ESP_PARTITION_SUBTYPE_DATA_OTA) {
        result.kind = PartitionKind::kOtaData;
    }
    return result;
}

Result OtaResult(esp_err_t result) {
    switch (result) {
        case ESP_OK: return Result::kOk;
        case ESP_ERR_OTA_VALIDATE_FAILED:
        case ESP_ERR_IMAGE_INVALID:
        case ESP_ERR_OTA_SMALL_SEC_VER: return Result::kInvalidImage;
        case ESP_ERR_OTA_ROLLBACK_INVALID_STATE: return Result::kConfirmationRequired;
        case ESP_ERR_OTA_PARTITION_CONFLICT: return Result::kBootMismatch;
        case ESP_ERR_INVALID_ARG: return Result::kInvalidArgument;
        case ESP_ERR_INVALID_SIZE: return Result::kImageTooLarge;
        default: return Result::kIoError;
    }
}

Result VerifyHeader(const uint8_t* data, std::size_t size, uint32_t image_bytes) {
    static_assert(kFirmwareHeaderBytes == sizeof(esp_image_header_t) +
                  sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t));
    if (data == nullptr || size != kFirmwareHeaderBytes) return Result::kInvalidArgument;
    esp_image_header_t header;
    esp_image_segment_header_t first;
    esp_app_desc_t app;
    // Network fragments need not satisfy native structure alignment.
    std::memcpy(&header, data, sizeof(header));
    std::memcpy(&first, data + sizeof(header), sizeof(first));
    std::memcpy(&app, data + sizeof(header) + sizeof(first), sizeof(app));
    if (header.magic != ESP_IMAGE_HEADER_MAGIC || header.segment_count == 0 ||
        header.segment_count > ESP_IMAGE_MAX_SEGMENTS || header.hash_appended > 1 ||
        header.spi_mode > ESP_IMAGE_SPI_MODE_SLOW_READ ||
        (header.spi_speed > ESP_IMAGE_SPI_SPEED_DIV_4 && header.spi_speed != ESP_IMAGE_SPI_SPEED_DIV_1) ||
        header.spi_size >= ESP_IMAGE_FLASH_SIZE_MAX ||
        first.data_len < sizeof(app) || first.data_len % 4 != 0 ||
        app.magic_word != ESP_APP_DESC_MAGIC_WORD) return Result::kInvalidImage;
    const uint64_t minimum_segments = sizeof(header) +
        static_cast<uint64_t>(header.segment_count) * sizeof(first) + first.data_len;
    const uint64_t minimum_image = ((minimum_segments + 16) & ~uint64_t{15}) +
        (header.hash_appended ? 32 : 0);
    if (minimum_image > image_bytes) return Result::kInvalidImage;
    // Let IDF apply the chip ID, revision and eFuse compatibility rules.
    return bootloader_common_check_chip_validity(&header, ESP_IMAGE_APPLICATION) == ESP_OK
        ? Result::kOk : Result::kInvalidImage;
}

}  // namespace

EspUpdateBackend::~EspUpdateBackend() { AbortImage(); }

Result EspUpdateBackend::CheckImageTarget() const {
    const auto* running = esp_ota_get_running_partition();
    if (running == nullptr || !SamePartition(Copy(running), image_running_) ||
        !SamePartition(Copy(esp_ota_get_boot_partition()), image_running_)) return Result::kBootMismatch;
    if (!SamePartition(Copy(esp_ota_get_next_update_partition(running)), image_target_)) {
        return Result::kInvalidLayout;
    }
    return Result::kOk;
}

Result EspUpdateBackend::BeginImage(const Partition& target, uint32_t image_bytes,
                                    const uint8_t* header, std::size_t header_bytes) {
    if (ota_open_) return Result::kInvalidState;
    if (image_bytes == 0) return Result::kInvalidArgument;
    auto result = VerifyHeader(header, header_bytes, image_bytes);
    if (result != Result::kOk) return result;
    BootInfo info;
    result = ReadBootInfo(&info);
    if (result != Result::kOk) return result;
    Partition inactive;
    result = VerifyPartitions(info, &inactive);
    if (result != Result::kOk) return result;
    if (!SamePartition(target, inactive)) return Result::kInvalidLayout;
    if (image_bytes > target.size) return Result::kImageTooLarge;
    if (info.image_state == ImageState::kPendingVerify) return Result::kConfirmationRequired;
    if (info.image_state != ImageState::kFactory && info.image_state != ImageState::kValid) {
        return Result::kInvalidState;
    }
    image_target_ = target;
    image_running_ = info.running;
    image_bytes_ = image_bytes;
    written_bytes_ = 0;
    result = CheckImageTarget();
    if (result != Result::kOk) return result;
    const auto* partition = esp_ota_get_next_update_partition(esp_ota_get_running_partition());
    // Erase sectors as sequential writes arrive, while enforcing the declared size here.
    result = OtaResult(esp_ota_begin(partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle_));
    ota_open_ = result == Result::kOk;
    return result;
}

Result EspUpdateBackend::WriteImage(const uint8_t* data, std::size_t size) {
    if (!ota_open_) return Result::kInvalidState;
    if (data == nullptr || size == 0 || size > kMaximumFirmwareChunkBytes ||
        size > image_bytes_ - written_bytes_) return Result::kInvalidArgument;
    auto result = CheckImageTarget();
    if (result == Result::kOk) result = OtaResult(esp_ota_write(ota_handle_, data, size));
    if (result != Result::kOk) {
        AbortImage();
        return result;
    }
    written_bytes_ += static_cast<uint32_t>(size);
    return Result::kOk;
}

Result EspUpdateBackend::CommitImage(uint32_t expected_crc32) {
    if (!ota_open_) return Result::kInvalidState;
    if (written_bytes_ != image_bytes_) return Result::kIncompleteImage;
    auto result = CheckImageTarget();
    if (result != Result::kOk) {
        AbortImage();
        return result;
    }
    const auto* partition = esp_ota_get_next_update_partition(esp_ota_get_running_partition());
    // IDF validates segments, checksum and existing digest/signature policy.
    result = OtaResult(esp_ota_end(ota_handle_));
    ota_open_ = false;  // IDF consumes the handle even when validation fails.
    if (result == Result::kOk) result = CheckImageTarget();
    if (result == Result::kOk) {
        // esp_ota_end verifies the whole slot. Bound the parsed image, including
        // any native signature, to the bytes actually received, not an old tail.
        const esp_partition_pos_t received{partition->address, image_bytes_};
        esp_image_metadata_t metadata;
        result = OtaResult(esp_image_get_metadata(&received, &metadata));
    }
    uint32_t crc = 0;
    std::array<uint8_t, 1024> buffer;
    for (uint32_t offset = 0; result == Result::kOk && offset < image_bytes_;) {
        const auto count = std::min<std::size_t>(buffer.size(), image_bytes_ - offset);
        if (esp_partition_read(partition, offset, buffer.data(), count) != ESP_OK) {
            result = Result::kIoError;
            break;
        }
        crc = FirmwareCrc32(buffer.data(), count, crc);
        offset += static_cast<uint32_t>(count);
    }
    if (result == Result::kOk && crc != expected_crc32) result = Result::kChecksumMismatch;
    if (result == Result::kOk) result = CheckImageTarget();
    if (result == Result::kOk) result = OtaResult(esp_ota_set_boot_partition(partition));
    AbortImage();
    return result;
}

void EspUpdateBackend::AbortImage() {
    if (ota_open_) esp_ota_abort(ota_handle_);
    ota_open_ = false;
    ota_handle_ = 0;
    image_target_ = {};
    image_running_ = {};
    image_bytes_ = written_bytes_ = 0;
}

}  // namespace zectrix::update
