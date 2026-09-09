#include "zectrix_boot_esp.h"

#include <algorithm>

#include "esp_flash.h"
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

}  // namespace

Result EspBootBackend::ReadBootInfo(BootInfo* info) {
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

uint64_t EspBootBackend::Milliseconds() const {
    return static_cast<uint64_t>(esp_timer_get_time()) / 1000;
}

Result EspBootBackend::ArmBootWatchdog(uint32_t timeout_ms) {
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

void EspBootBackend::DisarmBootWatchdog() {
    wdt_hal_context_t context = RWDT_HAL_CONTEXT_DEFAULT();
    wdt_hal_write_protect_disable(&context);
    wdt_hal_disable(&context);
    wdt_hal_write_protect_enable(&context);
}

Result EspBootBackend::ConfirmRunningImage(const Partition& expected) {
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

}  // namespace zectrix::update
