#include "zectrix_update_esp.h"
#include "update_test_fixture.h"

#include <cassert>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#include "bootloader_common.h"
#include "esp_app_desc.h"
#include "esp_flash.h"
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "hal/wdt_hal.h"
#include "soc/rtc.h"

using namespace zectrix::update;

namespace {

struct FakeBackend final : UpdateBackend {
    BootInfo info = UpdateBootFixture();
    uint64_t now = 100;
    uint32_t read_delay = 0;
    bool armed = false;
    Result read_result = Result::kOk;
    Result arm_result = Result::kOk;
    Result confirm_result = Result::kOk;
    std::vector<std::string> calls;

    Result ReadBootInfo(BootInfo* output) override {
        calls.emplace_back("read");
        now += read_delay;
        *output = info;
        return read_result;
    }
    uint64_t Milliseconds() const override { return now; }
    Result ArmBootWatchdog(uint32_t timeout_ms) override {
        assert(timeout_ms == kBootConfirmationTimeoutMs);
        calls.emplace_back("arm");
        armed = arm_result == Result::kOk;
        return arm_result;
    }
    void DisarmBootWatchdog() override { calls.emplace_back("disarm"); armed = false; }
    Result ConfirmRunningImage(const Partition& expected) override {
        assert(armed && SamePartition(expected, info.running));
        calls.emplace_back("confirm");
        if (confirm_result == Result::kOk) info.image_state = ImageState::kValid;
        return confirm_result;
    }
    Result BeginImage(const Partition&, uint32_t, const uint8_t*, std::size_t) override {
        assert(false);
        return Result::kInvalidState;
    }
    Result WriteImage(const uint8_t*, std::size_t) override {
        assert(false);
        return Result::kInvalidState;
    }
    Result CommitImage(uint32_t) override {
        assert(false);
        return Result::kInvalidState;
    }
    void AbortImage() override {}
};

void TestPartitionSelection() {
    for (auto running : {PartitionKind::kFactory, PartitionKind::kOtaA, PartitionKind::kOtaB}) {
        const auto info = UpdateBootFixture(running);
        Partition target;
        assert(VerifyPartitions(info, &target) == Result::kOk);
        assert(target.kind == (running == PartitionKind::kOtaA ? PartitionKind::kOtaB : PartitionKind::kOtaA));
        assert(!SamePartition(target, info.running));
    }
    auto info = UpdateBootFixture();
    Partition target;
    info.boot = info.next_update;
    assert(VerifyPartitions(info, &target) == Result::kBootMismatch);
    assert(target.size == 0);
    info = UpdateBootFixture(PartitionKind::kOtaA);
    info.next_update = info.running;
    assert(VerifyPartitions(info, &target) == Result::kInvalidLayout);
    info = UpdateBootFixture();
    info.running.size -= 0x1000;
    assert(VerifyPartitions(info, &target) == Result::kInvalidLayout);
    assert(VerifyPartitions(info, nullptr) == Result::kInvalidArgument);
}

void TestInvalidLayouts() {
    using Mutation = void (*)(BootInfo&);
    const Mutation mutations[] = {
        [](BootInfo& i) { i.flash_bytes = 8 * 1024 * 1024; },
        [](BootInfo& i) { i.partition_count = 0; },
        [](BootInfo& i) { i.partition_count = kMaximumPartitions + 1; },
        [](BootInfo& i) { i.partition_table_address = 0xffffffff; },
        [](BootInfo& i) { i.partitions[3].address += 0x1000; },
        [](BootInfo& i) { i.partitions[4].address = i.partitions[3].address; },
        [](BootInfo& i) { i.partitions[4].size = 0xfffff000; },
        [](BootInfo& i) { i.partitions[4].size = 0; },
        [](BootInfo& i) { i.partitions[4].size -= 1; },
        [](BootInfo& i) { i.partitions[5].size = 0x1000; },
        [](BootInfo& i) { i.partitions[5].kind = PartitionKind::kOther; },
        [](BootInfo& i) { i.partitions[5].address = 0x8000; },
        [](BootInfo& i) { i.partitions[5].readonly = true; },
        [](BootInfo& i) { i.partitions[3].readonly = true; },
        [](BootInfo& i) { i.partitions[4].readonly = true; },
        [](BootInfo& i) { i.partitions[4].kind = PartitionKind::kOtaA; },
        [](BootInfo& i) { i.partitions[4].kind = PartitionKind::kOtherOta; },
        [](BootInfo& i) { i.partitions[6] = {PartitionKind::kOther, 0x310000, 0x1000}; ++i.partition_count; },
        [](BootInfo& i) { i.partitions[6] = {PartitionKind::kOtaData, 0xa00000, 0x2000}; ++i.partition_count; },
    };
    for (auto mutate : mutations) {
        auto info = UpdateBootFixture();
        mutate(info);
        Partition target = info.next_update;
        assert(VerifyPartitions(info, &target) == Result::kInvalidLayout);
        assert(target.size == 0);
    }
}

void TestBootConfirmationAndTargetRefresh() {
    FakeBackend backend;
    backend.info = UpdateBootFixture(PartitionKind::kOtaA);
    backend.info.image_state = ImageState::kPendingVerify;
    UpdateService service(backend);
    assert(service.BeginBoot() == Result::kOk);
    assert((backend.calls == std::vector<std::string>{"arm", "read"}));
    assert(service.BeginBoot() == Result::kOk);
    assert(backend.calls.size() == 2);
    assert(service.ReadBootStatus().confirmation_pending);
    Partition target;
    assert(service.SelectUpdateTarget(1, &target) == Result::kConfirmationRequired);
    assert(backend.armed);
    backend.now += kBootConfirmationTimeoutMs - 1;
    assert(service.ConfirmBoot() == Result::kOk);
    assert((backend.calls == std::vector<std::string>{"arm", "read", "read", "confirm", "disarm"}));
    assert(!backend.armed && !service.ReadBootStatus().confirmation_pending);
    assert(service.ConfirmBoot() == Result::kOk);
    assert(backend.calls.size() == 5);
    assert(service.SelectUpdateTarget(0x300000, &target) == Result::kOk);
    assert(target.kind == PartitionKind::kOtaB);
    assert(service.SelectUpdateTarget(0x300001, &target) == Result::kImageTooLarge);
    assert(target.size == 0);
    assert(service.SelectUpdateTarget(0, &target) == Result::kInvalidArgument);
    assert(service.SelectUpdateTarget(1, nullptr) == Result::kInvalidArgument);
    // Scheduling another boot after startup must revoke this target selection.
    backend.info.boot = backend.info.next_update;
    assert(service.SelectUpdateTarget(1, &target) == Result::kBootMismatch);
    assert(target.size == 0);
}

void TestFailedAndUnconfirmedBoots() {
    for (bool delay_read : {false, true}) {
        FakeBackend backend;
        backend.info = UpdateBootFixture(PartitionKind::kOtaA);
        backend.info.image_state = ImageState::kPendingVerify;
        {
            UpdateService service(backend);
            assert(service.BeginBoot() == Result::kOk);
            if (delay_read) backend.read_delay = kBootConfirmationTimeoutMs;
            else backend.now += kBootConfirmationTimeoutMs;
            assert(service.ConfirmBoot() == Result::kTimeout);
            assert(service.ConfirmBoot() == Result::kInvalidState);
            assert(service.BeginBoot() == Result::kInvalidState);
        }
        assert(backend.armed);
        for (const auto& call : backend.calls) assert(call != "confirm" && call != "disarm");
    }
    for (auto state : {ImageState::kNew, ImageState::kUnknown, ImageState::kUndefined,
                       ImageState::kInvalid, ImageState::kAborted}) {
        FakeBackend backend;
        backend.info = UpdateBootFixture(PartitionKind::kOtaA);
        backend.info.image_state = state;
        UpdateService service(backend);
        assert(service.BeginBoot() == Result::kInvalidState);
        assert(backend.armed);
    }
    for (int failure = 0; failure < 4; ++failure) {
        FakeBackend backend;
        backend.info = UpdateBootFixture(PartitionKind::kOtaA);
        backend.info.image_state = ImageState::kPendingVerify;
        UpdateService service(backend);
        assert(service.BeginBoot() == Result::kOk);
        if (failure == 0) backend.confirm_result = Result::kIoError;
        if (failure == 1) backend.read_result = Result::kIoError;
        if (failure == 2) backend.info.boot = backend.info.next_update;
        if (failure == 3) backend.info.image_state = ImageState::kValid;
        assert(service.ConfirmBoot() != Result::kOk);
        assert(backend.armed);
        Partition target;
        assert(service.SelectUpdateTarget(1, &target) == Result::kInvalidState);
    }
    FakeBackend failed_read;
    failed_read.read_result = Result::kIoError;
    UpdateService failed(failed_read);
    assert(failed.BeginBoot() == Result::kIoError);
    assert(failed_read.armed);
    FakeBackend unprotected;
    unprotected.arm_result = Result::kInvalidState;
    UpdateService unavailable(unprotected);
    assert(unavailable.BeginBoot() == Result::kInvalidState);
    assert((unprotected.calls == std::vector<std::string>{"arm"}));
    FakeBackend wrong_boot;
    wrong_boot.info = UpdateBootFixture(PartitionKind::kOtaA);
    wrong_boot.info.image_state = ImageState::kPendingVerify;
    wrong_boot.info.boot = wrong_boot.info.next_update;
    UpdateService mismatched(wrong_boot);
    assert(mismatched.BeginBoot() == Result::kBootMismatch && wrong_boot.armed);
}

void TestRecoveryAndKnownGoodBoots() {
    for (auto running : {PartitionKind::kFactory, PartitionKind::kOtaA, PartitionKind::kOtaB}) {
        FakeBackend backend;
        backend.info = UpdateBootFixture(running);
        UpdateService service(backend);
        assert(service.BeginBoot() == Result::kOk);
        assert(!backend.armed);
        assert((backend.calls == std::vector<std::string>{"arm", "read", "disarm"}));
        assert(service.ConfirmBoot() == Result::kOk);
    }
    FakeBackend recovery;
    recovery.info.partition_count = 3;
    recovery.info.next_update = {};
    UpdateService service(recovery);
    assert(service.BeginBoot() == Result::kOk);
    assert(!recovery.armed);
    Partition target;
    assert(service.SelectUpdateTarget(1, &target) == Result::kInvalidLayout);
    assert(service.ReadBootStatus().layout_result == Result::kInvalidLayout);
    // Missing fallback is reported, but a healthy trial can preserve itself.
    FakeBackend trial;
    trial.info = UpdateBootFixture(PartitionKind::kOtaB);
    trial.info.image_state = ImageState::kPendingVerify;
    trial.info.rollback_available = false;
    UpdateService only_bootable(trial);
    assert(only_bootable.BeginBoot() == Result::kOk);
    assert(only_bootable.ConfirmBoot() == Result::kOk);
}

std::vector<esp_partition_t> native_partitions;
std::size_t running_index = 2, boot_index = 2, next_index = 3;
esp_ota_img_states_t native_state = ESP_OTA_IMG_VALID;
esp_err_t flash_result = ESP_OK, state_result = ESP_OK, confirm_result = ESP_OK;
uint32_t configured_flash = 16 * 1024 * 1024, physical_flash = configured_flash;
uint32_t slow_clock_hz = 32768, watchdog_ticks = 0;
int64_t now_us = 0;
unsigned active_iterators = 0, state_reads = 0, confirmations = 0;
bool watchdog_enabled = false, watchdog_unlocked = false;
bool rollback_available = true;
std::vector<uint8_t> native_flash;
esp_err_t begin_image_result = ESP_OK, end_image_result = ESP_OK, select_image_result = ESP_OK;
esp_err_t chip_result = ESP_OK;
esp_err_t metadata_result = ESP_OK;
unsigned image_begins = 0, image_writes = 0, image_ends = 0, image_aborts = 0;
unsigned image_reads = 0, image_selections = 0, fail_write_at = 0, fail_read_at = 0;
unsigned metadata_reads = 0;
bool writer_open = false, change_boot_on_end = false, select_before_error = false;
constexpr esp_ota_handle_t kNativeHandle = 42;

void ResetNative(PartitionKind running = PartitionKind::kFactory) {
    assert(active_iterators == 0 && !writer_open);
    const auto info = UpdateBootFixture(running);
    native_partitions.clear();
    for (std::size_t index = 0; index < info.partition_count; ++index) {
        const auto& p = info.partitions[index];
        esp_partition_type_t type = ESP_PARTITION_TYPE_APP;
        esp_partition_subtype_t subtype = ESP_PARTITION_SUBTYPE_APP_FACTORY;
        if (p.kind == PartitionKind::kOtaA) subtype = ESP_PARTITION_SUBTYPE_APP_OTA_0;
        if (p.kind == PartitionKind::kOtaB) subtype = ESP_PARTITION_SUBTYPE_APP_OTA_1;
        if (p.kind == PartitionKind::kOtaData || p.kind == PartitionKind::kOther) {
            type = ESP_PARTITION_TYPE_DATA;
            subtype = p.kind == PartitionKind::kOtaData ? ESP_PARTITION_SUBTYPE_DATA_OTA : ESP_PARTITION_SUBTYPE_ANY;
        }
        native_partitions.push_back({type, subtype, p.address, p.size, p.readonly});
    }
    running_index = boot_index = running == PartitionKind::kFactory ? 2 : running == PartitionKind::kOtaA ? 3 : 4;
    next_index = running == PartitionKind::kOtaA ? 4 : 3;
    native_state = ESP_OTA_IMG_VALID;
    flash_result = state_result = confirm_result = ESP_OK;
    configured_flash = physical_flash = 16 * 1024 * 1024;
    slow_clock_hz = 32768;
    watchdog_ticks = state_reads = confirmations = 0;
    now_us = 0;
    watchdog_enabled = watchdog_unlocked = false;
    rollback_available = true;
    native_flash.clear();
    begin_image_result = end_image_result = select_image_result = chip_result = ESP_OK;
    metadata_result = ESP_OK;
    image_begins = image_writes = image_ends = image_aborts = 0;
    image_reads = image_selections = fail_write_at = fail_read_at = 0;
    metadata_reads = 0;
    change_boot_on_end = select_before_error = false;
}

void TestEspDriver() {
    EspUpdateBackend driver;
    ResetNative();
    BootInfo info;
    Partition target;
    assert(driver.ReadBootInfo(nullptr) == Result::kInvalidArgument);
    assert(driver.ReadBootInfo(&info) == Result::kOk);
    assert(info.image_state == ImageState::kFactory && state_reads == 0);
    assert(info.partition_count == 6 && active_iterators == 0);
    assert(VerifyPartitions(info, &target) == Result::kOk);
    assert(driver.ArmBootWatchdog(60000) == Result::kOk);
    assert(watchdog_enabled && !watchdog_unlocked && watchdog_ticks == 60 * slow_clock_hz);
    driver.DisarmBootWatchdog();
    assert(!watchdog_enabled && !watchdog_unlocked);
    assert(driver.ArmBootWatchdog(0) == Result::kInvalidArgument);
    slow_clock_hz = 0;
    assert(driver.ArmBootWatchdog(60000) == Result::kInvalidState);
    slow_clock_hz = std::numeric_limits<uint32_t>::max();
    assert(driver.ArmBootWatchdog(60000) == Result::kInvalidState);
    physical_flash = 8 * 1024 * 1024;
    assert(driver.ReadBootInfo(&info) == Result::kOk);
    assert(info.flash_bytes == physical_flash);
    assert(VerifyPartitions(info, &target) == Result::kInvalidLayout);
    physical_flash = 32 * 1024 * 1024;
    assert(driver.ReadBootInfo(&info) == Result::kOk && info.flash_bytes == configured_flash);
    native_partitions.resize(kMaximumPartitions + 1);
    assert(driver.ReadBootInfo(&info) == Result::kInvalidLayout && active_iterators == 0);
    ResetNative();
    native_partitions[4].subtype = static_cast<esp_partition_subtype_t>(0x12);
    assert(driver.ReadBootInfo(&info) == Result::kOk);
    assert(VerifyPartitions(info, &target) == Result::kInvalidLayout);

    ResetNative(PartitionKind::kOtaA);
    native_state = ESP_OTA_IMG_PENDING_VERIFY;
    {
        UpdateService trial(driver);
        assert(trial.BeginBoot() == Result::kOk && watchdog_enabled);
        assert(trial.ReadBootStatus().rollback_available);
        assert(trial.ConfirmBoot() == Result::kOk);
        assert(native_state == ESP_OTA_IMG_VALID && confirmations == 1 && !watchdog_enabled);
    }
    ResetNative(PartitionKind::kOtaB);
    native_state = ESP_OTA_IMG_PENDING_VERIFY;
    {
        UpdateService trial(driver);
        assert(trial.BeginBoot() == Result::kOk);
        now_us = static_cast<int64_t>(kBootConfirmationTimeoutMs) * 1000;
        assert(trial.ConfirmBoot() == Result::kTimeout);
    }
    assert(watchdog_enabled && native_state == ESP_OTA_IMG_PENDING_VERIFY && confirmations == 0);
    ResetNative(PartitionKind::kOtaA);
    native_state = ESP_OTA_IMG_PENDING_VERIFY;
    assert(driver.ReadBootInfo(&info) == Result::kOk);
    boot_index = next_index;
    assert(driver.ConfirmRunningImage(info.running) == Result::kBootMismatch && confirmations == 0);
    boot_index = running_index;
    native_state = ESP_OTA_IMG_NEW;
    assert(driver.ConfirmRunningImage(info.running) == Result::kInvalidState && confirmations == 0);
    native_state = ESP_OTA_IMG_PENDING_VERIFY;
    state_result = ESP_FAIL;
    assert(driver.ReadBootInfo(&info) == Result::kIoError);
    state_result = ESP_OK;
    assert(driver.ReadBootInfo(&info) == Result::kOk);
    confirm_result = ESP_FAIL;
    assert(driver.ConfirmRunningImage(info.running) == Result::kIoError);
    assert(native_state == ESP_OTA_IMG_PENDING_VERIFY);
    flash_result = ESP_FAIL;
    assert(driver.ReadBootInfo(&info) == Result::kIoError);
}

std::vector<uint8_t> FirmwareImage(std::size_t size = 8240) {
    assert(size >= 304 && size % 16 == 0);
    std::vector<uint8_t> image(size);
    esp_image_header_t header{};
    header.magic = ESP_IMAGE_HEADER_MAGIC;
    header.segment_count = 1;
    header.spi_mode = ESP_IMAGE_SPI_MODE_DIO;
    header.spi_speed = ESP_IMAGE_SPI_SPEED_DIV_1;
    header.spi_size = 4;
    header.entry_addr = 0x40378000;
    header.wp_pin = 0xee;
    header.chip_id = ESP_CHIP_ID_ESP32S3;
    header.max_chip_rev_full = 0xffff;
    esp_image_segment_header_t first{0x3c000020, static_cast<uint32_t>(size - 48)};
    esp_app_desc_t app{};
    app.magic_word = ESP_APP_DESC_MAGIC_WORD;
    std::memcpy(image.data(), &header, sizeof(header));
    std::memcpy(image.data() + sizeof(header), &first, sizeof(first));
    std::memcpy(image.data() + 32, &app, sizeof(app));
    for (std::size_t index = kFirmwareHeaderBytes; index < 32 + first.data_len; ++index) {
        image[index] = static_cast<uint8_t>(index * 37 + 11);
    }
    uint8_t checksum = 0xef;
    for (std::size_t index = 32; index < 32 + first.data_len; ++index) checksum ^= image[index];
    image.back() = checksum;
    return image;
}

uint32_t ImageCrc(const std::vector<uint8_t>& image) {
    return FirmwareCrc32(image.data(), image.size());
}

Result WriteSlice(UpdateService& service, const std::vector<uint8_t>& image,
                  std::size_t offset, std::size_t count) {
    return service.WriteFirmwareChunk(static_cast<uint32_t>(offset), image.data() + offset,
                                      count, FirmwareCrc32(image.data() + offset, count));
}

void FeedImage(UpdateService& service, const std::vector<uint8_t>& image, std::size_t chunk) {
    while (service.ReadFirmwareStatus().received_bytes < image.size()) {
        const auto offset = service.ReadFirmwareStatus().received_bytes;
        const auto count = std::min(chunk, image.size() - offset);
        assert(WriteSlice(service, image, offset, count) == Result::kOk);
        assert(service.ReadFirmwareStatus().received_bytes == offset + count);
        if (offset + count < kFirmwareHeaderBytes) {
            assert(image_begins == 0 && native_flash.empty());
        }
        assert(image_ends == 0 && image_selections == 0);
    }
}

void TestFirmwareCrc() {
    const uint8_t text[] = "123456789";
    assert(FirmwareCrc32(nullptr, 0) == 0);
    assert(FirmwareCrc32(text, 9) == 0xcbf43926);
    for (std::size_t split = 0; split <= 9; ++split) {
        const auto first = FirmwareCrc32(text, split);
        assert(FirmwareCrc32(nullptr, 0, first) == first);
        assert(FirmwareCrc32(text + split, 9 - split, first) == 0xcbf43926);
    }
}

void TestStreamedFirmwareCommit(const std::vector<uint8_t>& image, std::size_t chunk,
                                PartitionKind running = PartitionKind::kFactory) {
    ResetNative(running);
    EspUpdateBackend driver;
    UpdateService service(driver);
    assert(service.BeginBoot() == Result::kOk);
    assert(service.BeginFirmware(static_cast<uint32_t>(image.size()), ImageCrc(image)) == Result::kOk);
    assert(image_begins == 0 && service.ReadFirmwareStatus().phase == FirmwarePhase::kReceiving);
    FeedImage(service, image, chunk);
    assert(native_flash == image && image_begins == 1 && writer_open);
    assert(boot_index == running_index);
    assert(service.CommitFirmware() == Result::kOk);
    assert(image_ends == 1 && metadata_reads == 1 && image_reads == (image.size() + 1023) / 1024 && image_selections == 1);
    assert(image_aborts == 0 && !writer_open && boot_index == next_index);
    assert(service.ReadFirmwareStatus().phase == FirmwarePhase::kCommitted);
    assert(SamePartition(service.ReadBootStatus().boot, service.ReadFirmwareStatus().target));
    assert(service.ReadBootStatus().layout_result == Result::kBootMismatch);
    Partition target;
    assert(service.SelectUpdateTarget(static_cast<uint32_t>(image.size()), &target) == Result::kBootMismatch);
    assert(service.CommitFirmware() == Result::kInvalidState);
    assert(service.BeginFirmware(static_cast<uint32_t>(image.size()), ImageCrc(image)) == Result::kInvalidState);
    service.AbortFirmware();
    assert(service.ReadFirmwareStatus().phase == FirmwarePhase::kCommitted && image_selections == 1);
}

void TestChunkRetriesAndBounds() {
    ResetNative();
    const auto image = FirmwareImage();
    EspUpdateBackend driver;
    UpdateService service(driver);
    assert(service.BeginFirmware(image.size(), ImageCrc(image)) == Result::kInvalidState);
    assert(service.BeginBoot() == Result::kOk);
    assert(service.BeginFirmware(0, 0) == Result::kInvalidArgument);
    assert(service.BeginFirmware(kFirmwareHeaderBytes, 0) == Result::kInvalidArgument);
    assert(service.BeginFirmware(0x300001, 0) == Result::kImageTooLarge);
    assert(service.CommitFirmware() == Result::kInvalidState);
    assert(WriteSlice(service, image, 0, 1) == Result::kInvalidState);
    assert(service.BeginFirmware(image.size(), ImageCrc(image)) == Result::kOk);
    assert(service.BeginFirmware(image.size(), ImageCrc(image)) == Result::kInvalidState);
    assert(service.CommitFirmware() == Result::kIncompleteImage);
    assert(service.WriteFirmwareChunk(0, nullptr, 1, 0) == Result::kInvalidArgument);
    assert(service.WriteFirmwareChunk(0, image.data(), 0, 0) == Result::kInvalidArgument);
    assert(service.WriteFirmwareChunk(0, image.data(), kMaximumFirmwareChunkBytes + 1, 0) == Result::kInvalidArgument);
    assert(service.WriteFirmwareChunk(0, image.data(), std::numeric_limits<std::size_t>::max(), 0) == Result::kInvalidArgument);
    assert(WriteSlice(service, image, 0, 7) == Result::kOk);
    assert(image_begins == 0 && service.ReadFirmwareStatus().received_bytes == 7);
    assert(WriteSlice(service, image, 0, 7) == Result::kUnexpectedOffset);
    assert(WriteSlice(service, image, 8, 7) == Result::kUnexpectedOffset);
    assert(service.WriteFirmwareChunk(UINT32_MAX, image.data(), 1, 0) == Result::kUnexpectedOffset);
    assert(service.WriteFirmwareChunk(7, image.data() + 7, 300,
        FirmwareCrc32(image.data() + 7, 300) ^ 1) == Result::kChecksumMismatch);
    assert(image_begins == 0 && service.ReadFirmwareStatus().received_bytes == 7);
    assert(WriteSlice(service, image, 7, 300) == Result::kOk);
    const auto before = native_flash;
    assert(service.CommitFirmware() == Result::kIncompleteImage && writer_open);
    assert(service.WriteFirmwareChunk(307, image.data() + 307, 100,
        FirmwareCrc32(image.data() + 307, 100) ^ 1) == Result::kChecksumMismatch);
    assert(native_flash == before && service.ReadFirmwareStatus().received_bytes == 307);
    FeedImage(service, image, 113);
    assert(service.WriteFirmwareChunk(image.size(), image.data(), 1, 0) == Result::kInvalidArgument);
    assert(service.CommitFirmware() == Result::kOk && native_flash == image);
}

void TestMalformedFirmwareHeaders() {
    using Mutation = void (*)(std::vector<uint8_t>&);
    const Mutation mutations[] = {
        [](auto& i) { i[0] = 0; },
        [](auto& i) { i[1] = 0; },
        [](auto& i) { i[1] = 17; },
        [](auto& i) { i[1] = 16; },
        [](auto& i) { i[2] = 0xff; },
        [](auto& i) { i[3] = 0x43; },
        [](auto& i) { i[3] = 0xff; },
        [](auto& i) { i[12] = 0; },
        [](auto& i) { i[23] = 1; },
        [](auto& i) { i[23] = 2; },
        [](auto& i) { std::fill(i.begin() + 28, i.begin() + 32, 0xff); },
        [](auto& i) { i[28] = 252; i[29] = i[30] = i[31] = 0; },
        [](auto& i) { i[28] |= 1; },
        [](auto& i) { i[32] ^= 1; },
    };
    for (auto mutate : mutations) {
        ResetNative();
        auto image = FirmwareImage();
        mutate(image);
        EspUpdateBackend driver;
        UpdateService service(driver);
        assert(service.BeginBoot() == Result::kOk);
        assert(service.BeginFirmware(image.size(), ImageCrc(image)) == Result::kOk);
        assert(WriteSlice(service, image, 0, 17) == Result::kOk);
        assert(WriteSlice(service, image, 17, kFirmwareHeaderBytes - 17) == Result::kInvalidImage);
        assert(service.ReadFirmwareStatus().phase == FirmwarePhase::kFailed);
        assert(image_begins == 0 && image_writes == 0 && !writer_open && boot_index == running_index);
        assert(service.CommitFirmware() == Result::kInvalidState);
    }
    ResetNative();
    EspUpdateBackend driver;
    UpdateService service(driver);
    const auto image = FirmwareImage();
    assert(service.BeginBoot() == Result::kOk);
    assert(service.BeginFirmware(image.size(), ImageCrc(image)) == Result::kOk);
    chip_result = ESP_FAIL;
    assert(WriteSlice(service, image, 0, kFirmwareHeaderBytes) == Result::kInvalidImage);
    assert(image_begins == 0);
    chip_result = ESP_OK;
    assert(service.BeginFirmware(image.size(), ImageCrc(image)) == Result::kOk);
    FeedImage(service, image, 4096);
    assert(service.CommitFirmware() == Result::kOk);
}

void TestFirmwareFailures() {
    const auto image = FirmwareImage();
    for (unsigned failure = 0; failure < 11; ++failure) {
        ResetNative();
        EspUpdateBackend driver;
        UpdateService service(driver);
        assert(service.BeginBoot() == Result::kOk);
        const auto expected_crc = ImageCrc(image) ^ (failure == 4 ? 1 : 0);
        assert(service.BeginFirmware(image.size(), expected_crc) == Result::kOk);
        if (failure == 0) begin_image_result = ESP_ERR_NO_MEM;
        if (failure == 1) fail_write_at = 1;
        if (failure == 2) fail_write_at = 2;
        if (failure < 3) {
            assert(WriteSlice(service, image, 0, 1024) == Result::kIoError);
            assert(image_aborts == (failure == 0 ? 0 : 1));
            assert(image_ends == 0);
        } else {
            FeedImage(service, image, 1024);
            if (failure == 3) end_image_result = ESP_ERR_OTA_VALIDATE_FAILED;
            if (failure == 5) native_flash[5000] ^= 1;
            if (failure == 6) fail_read_at = 2;
            if (failure == 7 || failure == 10) select_image_result = ESP_FAIL;
            if (failure == 10) select_before_error = true;
            if (failure == 8) metadata_result = ESP_ERR_IMAGE_INVALID;
            if (failure == 9) metadata_result = ESP_ERR_IMAGE_FLASH_FAIL;
            const auto expected = (failure == 3 || failure == 8) ? Result::kInvalidImage :
                failure <= 5 ? Result::kChecksumMismatch : Result::kIoError;
            assert(service.CommitFirmware() == expected);
            assert(image_ends == (failure == 4 ? 0 : 1));
            assert(image_aborts == (failure == 4 ? 1 : 0));
        }
        assert(!writer_open && service.ReadFirmwareStatus().phase == FirmwarePhase::kFailed);
        assert(boot_index == (failure == 10 ? next_index : running_index));
        assert(image_selections == (failure == 7 || failure == 10 ? 1 : 0));
        assert(service.CommitFirmware() == Result::kInvalidState);
        assert(WriteSlice(service, image, 0, 1) == Result::kInvalidState);
        if (failure == 10) {
            // An I/O error can follow a durable selection. Never overwrite that slot.
            assert(service.BeginFirmware(image.size(), ImageCrc(image)) == Result::kBootMismatch);
        }
    }
}

void TestInterruptedAndRevokedTransfers() {
    const auto image = FirmwareImage();
    for (auto prefix : {17u, 300u}) {
        ResetNative();
        EspUpdateBackend driver;
        {
            UpdateService service(driver);
            assert(service.BeginBoot() == Result::kOk);
            assert(service.BeginFirmware(image.size(), ImageCrc(image)) == Result::kOk);
            assert(WriteSlice(service, image, 0, prefix) == Result::kOk);
            service.AbortFirmware();
            service.AbortFirmware();
            assert(image_aborts == (prefix == 17 ? 0 : 1));
            assert(service.ReadFirmwareStatus().phase == FirmwarePhase::kIdle);
            assert(service.BeginFirmware(image.size(), ImageCrc(image)) == Result::kOk);
            assert(WriteSlice(service, image, 0, prefix) == Result::kOk);
        }
        assert(image_aborts == (prefix == 17 ? 0 : 2));
        assert(!writer_open && image_selections == 0 && boot_index == running_index);
    }
    for (unsigned stage = 0; stage < 5; ++stage) {
        ResetNative();
        EspUpdateBackend driver;
        UpdateService service(driver);
        assert(service.BeginBoot() == Result::kOk);
        assert(service.BeginFirmware(image.size(), ImageCrc(image)) == Result::kOk);
        if (stage >= 2) FeedImage(service, image, 4096);
        else if (stage == 1) assert(WriteSlice(service, image, 0, 300) == Result::kOk);
        if (stage == 3) change_boot_on_end = true;
        else if (stage == 4) next_index = running_index;
        else boot_index = next_index;
        const auto result = stage >= 2 ? service.CommitFirmware() :
            WriteSlice(service, image, stage == 0 ? 0 : 300, 300);
        assert(result == (stage == 4 ? Result::kInvalidLayout : Result::kBootMismatch));
        assert(!writer_open && image_selections == 0);
        assert(image_begins == (stage == 0 ? 0 : 1));
        assert(image_ends == (stage == 3 ? 1 : 0));
        assert(image_reads == 0);
    }
    ResetNative(PartitionKind::kOtaA);
    native_state = ESP_OTA_IMG_PENDING_VERIFY;
    EspUpdateBackend driver;
    {
        UpdateService service(driver);
        assert(service.BeginBoot() == Result::kOk);
        assert(service.BeginFirmware(image.size(), ImageCrc(image)) == Result::kConfirmationRequired);
    }
    assert(watchdog_enabled && image_begins == 0 && confirmations == 0);
}

void TestNativeWriterBoundsAndOwnership() {
    ResetNative();
    const auto image = FirmwareImage();
    const auto info = UpdateBootFixture();
    {
        EspUpdateBackend driver;
        assert(driver.WriteImage(image.data(), 1) == Result::kInvalidState);
        assert(driver.CommitImage(ImageCrc(image)) == Result::kInvalidState);
        assert(driver.BeginImage(info.next_update, image.size(), nullptr, kFirmwareHeaderBytes) == Result::kInvalidArgument);
        assert(driver.BeginImage(info.next_update, image.size(), image.data(), 24) == Result::kInvalidArgument);
        assert(driver.BeginImage(info.running, image.size(), image.data(), kFirmwareHeaderBytes) == Result::kInvalidLayout);
        assert(driver.BeginImage(info.next_update, 0x300001, image.data(), kFirmwareHeaderBytes) == Result::kImageTooLarge);
        assert(image_begins == 0);
        assert(driver.BeginImage(info.next_update, image.size(), image.data(), kFirmwareHeaderBytes) == Result::kOk);
        assert(driver.BeginImage(info.next_update, image.size(), image.data(), kFirmwareHeaderBytes) == Result::kInvalidState);
        assert(driver.WriteImage(nullptr, 1) == Result::kInvalidArgument);
        assert(driver.WriteImage(image.data(), kMaximumFirmwareChunkBytes + 1) == Result::kInvalidArgument);
        assert(driver.CommitImage(ImageCrc(image)) == Result::kIncompleteImage);
        assert(writer_open && image_writes == 0);
    }
    assert(!writer_open && image_aborts == 1 && image_selections == 0);
    ResetNative(PartitionKind::kOtaA);
    native_state = ESP_OTA_IMG_PENDING_VERIFY;
    EspUpdateBackend driver;
    const auto trial = UpdateBootFixture(PartitionKind::kOtaA);
    assert(driver.BeginImage(trial.next_update, image.size(), image.data(), kFirmwareHeaderBytes) == Result::kConfirmationRequired);
    assert(image_begins == 0);
}

}  // namespace

struct esp_partition_iterator_opaque_ { std::size_t index = 0; };
esp_partition_iterator_t esp_partition_find(esp_partition_type_t type, esp_partition_subtype_t subtype, const char*) {
    assert(type == ESP_PARTITION_TYPE_ANY && subtype == ESP_PARTITION_SUBTYPE_ANY);
    if (native_partitions.empty()) return nullptr;
    ++active_iterators;
    return new esp_partition_iterator_opaque_;
}
const esp_partition_t* esp_partition_get(esp_partition_iterator_t iterator) { return &native_partitions[iterator->index]; }
void esp_partition_iterator_release(esp_partition_iterator_t iterator) { --active_iterators; delete iterator; }
esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t iterator) {
    if (++iterator->index < native_partitions.size()) return iterator;
    esp_partition_iterator_release(iterator);
    return nullptr;
}
const esp_partition_t* esp_ota_get_running_partition() { return &native_partitions[running_index]; }
const esp_partition_t* esp_ota_get_boot_partition() { return &native_partitions[boot_index]; }
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t* running) {
    assert(running == esp_ota_get_running_partition());
    return &native_partitions[next_index];
}
esp_err_t esp_ota_get_state_partition(const esp_partition_t* running, esp_ota_img_states_t* state) {
    assert(running == esp_ota_get_running_partition());
    ++state_reads;
    *state = native_state;
    return state_result;
}
bool esp_ota_check_rollback_is_possible() { return rollback_available; }
esp_err_t esp_ota_mark_app_valid_cancel_rollback() {
    ++confirmations;
    if (confirm_result == ESP_OK) native_state = ESP_OTA_IMG_VALID;
    return confirm_result;
}
esp_err_t bootloader_common_check_chip_validity(const esp_image_header_t* header, esp_image_type type) {
    assert(type == ESP_IMAGE_APPLICATION);
    return header->chip_id == ESP_CHIP_ID_ESP32S3 ? chip_result : ESP_FAIL;
}
esp_err_t esp_ota_begin(const esp_partition_t* partition, std::size_t size, esp_ota_handle_t* handle) {
    assert(partition == &native_partitions[next_index] && next_index != running_index);
    assert(boot_index == running_index && !writer_open && !partition->readonly);
    assert(size == OTA_WITH_SEQUENTIAL_WRITES);
    ++image_begins;
    if (begin_image_result != ESP_OK) return begin_image_result;
    native_flash.clear();
    *handle = kNativeHandle;
    writer_open = true;
    return ESP_OK;
}
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void* data, std::size_t size) {
    assert(writer_open && handle == kNativeHandle && size > 0 && size <= kMaximumFirmwareChunkBytes);
    ++image_writes;
    const auto* bytes = static_cast<const uint8_t*>(data);
    if (fail_write_at == image_writes) {
        // A driver error need not imply that none of the bytes reached flash.
        native_flash.insert(native_flash.end(), bytes, bytes + size / 2);
        return ESP_FAIL;
    }
    native_flash.insert(native_flash.end(), bytes, bytes + size);
    return ESP_OK;
}
esp_err_t esp_ota_end(esp_ota_handle_t handle) {
    assert(writer_open && handle == kNativeHandle && !native_flash.empty());
    ++image_ends;
    writer_open = false;
    if (change_boot_on_end) boot_index = next_index;
    return end_image_result;
}
esp_err_t esp_ota_abort(esp_ota_handle_t handle) {
    assert(writer_open && handle == kNativeHandle);
    ++image_aborts;
    writer_open = false;
    return ESP_OK;
}
esp_err_t esp_image_get_metadata(const esp_partition_pos_t* partition, esp_image_metadata_t* metadata) {
    assert(!writer_open && image_ends == 1 && image_reads == 0 && image_selections == 0);
    // A slot-sized bound could let a truncated transfer reuse an older image tail.
    assert(partition->offset == native_partitions[next_index].address && partition->size == native_flash.size());
    ++metadata_reads;
    metadata->image_len = partition->size;
    return metadata_result;
}
esp_err_t esp_partition_read(const esp_partition_t* partition, std::size_t offset,
                             void* output, std::size_t size) {
    assert(!writer_open && image_ends == 1 && metadata_reads == 1 && image_selections == 0);
    assert(partition == &native_partitions[next_index] && size <= 1024 && size > 0);
    assert(offset + size <= native_flash.size());
    ++image_reads;
    if (fail_read_at == image_reads) return ESP_FAIL;
    std::memcpy(output, native_flash.data() + offset, size);
    return ESP_OK;
}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t* partition) {
    assert(!writer_open && image_ends == 1 && image_reads > 0 && image_selections == 0);
    assert(partition == &native_partitions[next_index] && boot_index == running_index);
    ++image_selections;
    if (select_image_result == ESP_OK || select_before_error) boot_index = next_index;
    return select_image_result;
}
esp_err_t esp_flash_get_size(void*, uint32_t* size) { *size = configured_flash; return flash_result; }
esp_err_t esp_flash_get_physical_size(void*, uint32_t* size) { *size = physical_flash; return flash_result; }
int64_t esp_timer_get_time() { return now_us; }
uint32_t rtc_clk_slow_freq_get_hz() { return slow_clock_hz; }
void wdt_hal_write_protect_disable(wdt_hal_context_t*) { assert(!watchdog_unlocked); watchdog_unlocked = true; }
void wdt_hal_write_protect_enable(wdt_hal_context_t*) { assert(watchdog_unlocked); watchdog_unlocked = false; }
void wdt_hal_config_stage(wdt_hal_context_t*, wdt_stage_t stage, uint32_t ticks, wdt_stage_action_t action) {
    assert(watchdog_unlocked && stage == WDT_STAGE0 && action == WDT_STAGE_ACTION_RESET_RTC);
    watchdog_ticks = ticks;
}
void wdt_hal_enable(wdt_hal_context_t*) { assert(watchdog_unlocked); watchdog_enabled = true; }
void wdt_hal_disable(wdt_hal_context_t*) { assert(watchdog_unlocked); watchdog_enabled = false; }

int main(int argc, char** argv) {
    if (argc > 2) {
        std::fprintf(stderr, "Usage: %s [firmware.bin]\n", argv[0]);
        return 2;
    }
    TestPartitionSelection();
    TestInvalidLayouts();
    TestBootConfirmationAndTargetRefresh();
    TestFailedAndUnconfirmedBoots();
    TestRecoveryAndKnownGoodBoots();
    TestEspDriver();
    TestFirmwareCrc();
    for (auto running : {PartitionKind::kFactory, PartitionKind::kOtaA, PartitionKind::kOtaB}) {
        for (auto chunk : {1u, 23u, 287u, 288u, 289u, 4096u}) {
            TestStreamedFirmwareCommit(FirmwareImage(), chunk, running);
        }
    }
    TestStreamedFirmwareCommit(FirmwareImage(0x300000), 4096);
    TestChunkRetriesAndBounds();
    TestMalformedFirmwareHeaders();
    TestFirmwareFailures();
    TestInterruptedAndRevokedTransfers();
    TestNativeWriterBoundsAndOwnership();
    if (argc == 2) {
        std::ifstream input(argv[1], std::ios::binary);
        if (!input) {
            std::fprintf(stderr, "Cannot read firmware image: %s\n", argv[1]);
            return 1;
        }
        const std::vector<uint8_t> image{std::istreambuf_iterator<char>(input),
                                       std::istreambuf_iterator<char>()};
        TestStreamedFirmwareCommit(image, 997);
        std::printf("PASS: firmware image bytes=%zu CRC-32=%08x through the Host OTA driver.\n",
                    image.size(), ImageCrc(image));
    }
}
