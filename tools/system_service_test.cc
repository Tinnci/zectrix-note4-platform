#include "zectrix_system_service.h"

#include <array>
#include <cassert>
#include <cstring>

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/task.h"
#include "zectrix_board.h"

namespace {
esp_app_desc_t app = {};
esp_reset_reason_t reset_reason = ESP_RST_POWERON;
esp_err_t flash_result = ESP_OK;
esp_err_t mac_result = ESP_OK;
unsigned task_count = 2;
bool task_count_changed = false;
}

const esp_app_desc_t* esp_app_get_description() { return &app; }
void esp_chip_info(esp_chip_info_t* info) {
    *info = {CHIP_FEATURE_WIFI_BGN | CHIP_FEATURE_BLE, 2, 3};
}
esp_reset_reason_t esp_reset_reason() { return reset_reason; }
esp_err_t esp_flash_get_size(void*, uint32_t* size) {
    if (flash_result == ESP_OK) *size = 16U * 1024U * 1024U;
    return flash_result;
}
esp_err_t esp_read_mac(uint8_t* mac, int) {
    if (mac_result == ESP_OK) {
        const uint8_t value[] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
        std::memcpy(mac, value, sizeof(value));
    }
    return mac_result;
}
size_t heap_caps_get_total_size(uint32_t capabilities) {
    return capabilities == MALLOC_CAP_SPIRAM ? 8U * 1024U * 1024U : 0;
}
size_t heap_caps_get_free_size(uint32_t) { return 1000; }
size_t heap_caps_get_minimum_free_size(uint32_t) { return 800; }
size_t heap_caps_get_largest_free_block(uint32_t) { return 600; }

UBaseType_t uxTaskGetNumberOfTasks() { return task_count; }
UBaseType_t uxTaskGetSystemState(TaskStatus_t* tasks, UBaseType_t capacity, uint32_t*) {
    if (capacity < task_count || task_count_changed) return 0;
    for (unsigned index = 0; index < task_count; ++index) {
        // A transient task name is deliberately unusable after the RTOS call.
        tasks[index] = {index == 0 ? host_current_task : reinterpret_cast<void*>(2),
                        reinterpret_cast<const char*>(1), index + 1,
                        index == 0 ? eRunning : eBlocked, index + 3, 1024 + index};
    }
    return task_count;
}

int main() {
    std::strcpy(app.project_name, "zectrix-note4");
    std::strcpy(app.version, "test-version");
    std::strcpy(app.idf_ver, "v5.5.2");
    std::strcpy(app.date, "Aug 11 2026");
    std::strcpy(app.time, "12:34:56");
    for (size_t index = 0; index < 32; ++index) app.app_elf_sha256[index] = index;

    ZectrixBoard board;
    zectrix::system::SystemService* service = nullptr;
    assert(zectrix::system::SystemService::Attach(board, nullptr) == ESP_ERR_INVALID_ARG);
    assert(zectrix::system::SystemService::Attach(board, &service) == ESP_OK);

    zectrix::system::SystemSnapshot snapshot;
    assert(service->ReadSnapshot(nullptr) == ESP_ERR_INVALID_ARG);
    assert(service->ReadSnapshot(&snapshot) == ESP_OK);
    assert(std::strcmp(snapshot.firmware.project_name.data(), "zectrix-note4") == 0);
    assert(std::strcmp(snapshot.firmware.version.data(), "test-version") == 0);
    assert(snapshot.firmware.app_elf_sha256[0] == '0');
    assert(snapshot.firmware.app_elf_sha256[63] == 'f');
    assert(snapshot.capabilities.core_count == 2);
    assert(snapshot.capabilities.chip_revision == 3);
    assert(snapshot.capabilities.wifi && snapshot.capabilities.bluetooth_le);
    assert(snapshot.capabilities.rtc && snapshot.capabilities.nfc);
    assert(snapshot.capabilities.psram);
    assert(snapshot.diagnostics.flash_bytes == 16U * 1024U * 1024U);
    assert(snapshot.diagnostics.psram_bytes == 8U * 1024U * 1024U);
    assert(snapshot.diagnostics.free_internal_heap_bytes == 1000);
    assert(snapshot.reset_reason == zectrix::system::ResetReason::PowerOn);
    assert(snapshot.wifi_mac[5] == 0x55);

    reset_reason = ESP_RST_TASK_WDT;
    assert(service->ReadSnapshot(&snapshot) == ESP_OK);
    assert(snapshot.reset_reason == zectrix::system::ResetReason::Watchdog);
    flash_result = ESP_FAIL;
    assert(service->ReadSnapshot(&snapshot) == ESP_FAIL);
    flash_result = ESP_OK;
    mac_result = ESP_FAIL;
    assert(service->ReadSnapshot(&snapshot) == ESP_FAIL);
    assert(service->ReadWifiMac(nullptr) == ESP_ERR_INVALID_ARG);
    zectrix::system::HeapSnapshot heap;
    assert(service->ReadHeap(&heap) == ESP_OK);
    assert(heap.internal.free == 1000 && heap.internal.minimum_free == 800);
    assert(heap.internal.largest_block == 600);
    assert(heap.psram.total == 8U * 1024U * 1024U);
    assert(service->ReadHeap(nullptr) == ESP_ERR_INVALID_ARG);

    zectrix::system::TaskSnapshot tasks;
    assert(service->ReadTasks(&tasks) == ESP_OK);
    assert(tasks.count == 2 && tasks.total == 2 && !tasks.capacity_exceeded);
    assert(tasks.tasks[0].id == 1 && tasks.tasks[0].application_owner);
    assert(tasks.tasks[1].state == zectrix::system::TaskState::kBlocked);
    assert(tasks.tasks[1].minimum_stack_bytes == 1025);
    task_count = zectrix::system::kMaximumTasks + 1;
    assert(service->ReadTasks(&tasks) == ESP_OK);
    assert(tasks.count == 0 && tasks.capacity_exceeded && tasks.total == task_count);
    task_count = 2;
    task_count_changed = true;
    assert(service->ReadTasks(&tasks) == ESP_OK && tasks.capacity_exceeded);
    assert(service->ReadTasks(nullptr) == ESP_ERR_INVALID_ARG);
    delete service;
}
