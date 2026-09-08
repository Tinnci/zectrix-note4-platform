#include "zectrix_update_esp.h"
#include "update_test_fixture.h"

#include <cassert>
#include <limits>
#include <string>
#include <vector>

#include "esp_flash.h"
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

void ResetNative(PartitionKind running = PartitionKind::kFactory) {
    assert(active_iterators == 0);
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

int main() {
    TestPartitionSelection();
    TestInvalidLayouts();
    TestBootConfirmationAndTargetRefresh();
    TestFailedAndUnconfirmedBoots();
    TestRecoveryAndKnownGoodBoots();
    TestEspDriver();
}
