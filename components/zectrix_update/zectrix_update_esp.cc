#include "zectrix_update_esp.h"

#include <algorithm>
#include <cstring>

#include "bootloader_common.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_flash.h"
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "hal/wdt_hal.h"
#include "sdkconfig.h"
#include "soc/rtc.h"

#if !CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE || !CONFIG_BOOTLOADER_WDT_DISABLE_IN_USER_CODE
#error "OTA boot protection requires bootloader rollback and the inherited RTC watchdog"
#endif

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

ImageState State(esp_ota_img_states_t state) {
    switch (state) {
        case ESP_OTA_IMG_NEW: return ImageState::kNew;
        case ESP_OTA_IMG_PENDING_VERIFY: return ImageState::kPendingVerify;
        case ESP_OTA_IMG_VALID: return ImageState::kValid;
        case ESP_OTA_IMG_INVALID: return ImageState::kInvalid;
        case ESP_OTA_IMG_ABORTED: return ImageState::kAborted;
        case ESP_OTA_IMG_UNDEFINED: return ImageState::kUndefined;
        default: return ImageState::kUnknown;
    }
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

Result EspUpdateBackend::ReadBootInfo(BootInfo* info) {
    if (info == nullptr) return Result::kInvalidArgument;
    *info = {};
    if (esp_flash_get_size(nullptr, &info->flash_bytes) != ESP_OK) return Result::kIoError;
    uint32_t physical_bytes = 0;
    if (esp_flash_get_physical_size(nullptr, &physical_bytes) != ESP_OK) return Result::kIoError;
    info->flash_bytes = std::min(info->flash_bytes, physical_bytes);
    info->partition_table_address = CONFIG_PARTITION_TABLE_OFFSET;
    auto iterator = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
    while (iterator != nullptr) {
        if (info->partition_count == info->partitions.size()) {
            esp_partition_iterator_release(iterator);
            return Result::kInvalidLayout;
        }
        const auto* partition = esp_partition_get(iterator);
        if (partition == nullptr) {
            esp_partition_iterator_release(iterator);
            return Result::kIoError;
        }
        info->partitions[info->partition_count++] = Copy(partition);
        iterator = esp_partition_next(iterator);
    }
    const auto* running = esp_ota_get_running_partition();
    if (running == nullptr) return Result::kIoError;
    info->running = Copy(running);
    info->boot = Copy(esp_ota_get_boot_partition());
    info->next_update = Copy(esp_ota_get_next_update_partition(running));
    if (info->running.kind == PartitionKind::kFactory) {
        info->image_state = ImageState::kFactory;
    } else {
        esp_ota_img_states_t state;
        if (esp_ota_get_state_partition(running, &state) != ESP_OK) return Result::kIoError;
        info->image_state = State(state);
        if (state == ESP_OTA_IMG_PENDING_VERIFY) {
            info->rollback_available = esp_ota_check_rollback_is_possible();
        }
    }
    return Result::kOk;
}

uint64_t EspUpdateBackend::Milliseconds() const {
    return static_cast<uint64_t>(esp_timer_get_time()) / 1000;
}

Result EspUpdateBackend::ArmBootWatchdog(uint32_t timeout_ms) {
    if (timeout_ms == 0 || timeout_ms > 120000) return Result::kInvalidArgument;
    const uint64_t ticks = static_cast<uint64_t>(rtc_clk_slow_freq_get_hz()) * timeout_ms / 1000;
    if (ticks == 0 || ticks > UINT32_MAX) return Result::kInvalidState;
    wdt_hal_context_t context = RWDT_HAL_CONTEXT_DEFAULT();
    wdt_hal_write_protect_disable(&context);
    wdt_hal_config_stage(&context, WDT_STAGE0, static_cast<uint32_t>(ticks), WDT_STAGE_ACTION_RESET_RTC);
    // This hardware deadline works even if initialization or scheduling stalls.
    wdt_hal_enable(&context);
    wdt_hal_write_protect_enable(&context);
    return Result::kOk;
}

void EspUpdateBackend::DisarmBootWatchdog() {
    wdt_hal_context_t context = RWDT_HAL_CONTEXT_DEFAULT();
    wdt_hal_write_protect_disable(&context);
    wdt_hal_disable(&context);
    wdt_hal_write_protect_enable(&context);
}

Result EspUpdateBackend::ConfirmRunningImage(const Partition& expected) {
    const auto* running = esp_ota_get_running_partition();
    const auto* boot = esp_ota_get_boot_partition();
    // IDF marks the active otadata entry, which can differ from the running app.
    if (running == nullptr || boot == nullptr || !SamePartition(Copy(running), expected) ||
        !SamePartition(Copy(boot), expected)) return Result::kBootMismatch;
    if (expected.kind != PartitionKind::kOtaA && expected.kind != PartitionKind::kOtaB) {
        return Result::kInvalidState;
    }
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK) return Result::kIoError;
    if (state != ESP_OTA_IMG_PENDING_VERIFY) return Result::kInvalidState;
    return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK ? Result::kOk : Result::kIoError;
}

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
