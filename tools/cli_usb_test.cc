#include "zectrix_cli_usb.h"
#include "zectrix_cli_log.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct CliHostSemaphore {
    std::mutex mutex;
    std::condition_variable changed;
    bool ready = false;
};

struct CliHostTask {
    std::thread worker;
    std::mutex mutex;
    std::condition_variable changed;
    bool notified = false;
    bool deleted = false;
};

namespace {
using namespace zectrix::cli;
using namespace std::chrono_literals;
struct TaskExit {};
thread_local TaskHandle_t current_task = nullptr;
std::vector<std::unique_ptr<CliHostTask>> tasks;
unsigned semaphore_calls = 0, fail_semaphore_at = 0;
std::atomic<unsigned> semaphores{0}, stale_notifications{0};
bool fail_task = false, delay_notification = false;
std::atomic<bool> installed{false}, vfs_driver{false}, capturing{false};
std::atomic<bool> blocked_output{false};
std::atomic<bool> usb_connected{true};
esp_err_t install_result = ESP_OK;
std::mutex input_mutex, output_mutex;
std::string input, output;
std::size_t read_limit = 512, write_limit = 512;

void Reset() {
    for (auto& task : tasks) task->worker.join();
    tasks.clear();
    assert(semaphores == 0 && !installed && !vfs_driver && !capturing);
    assert(stale_notifications == 0);
    semaphore_calls = fail_semaphore_at = 0;
    fail_task = delay_notification = false;
    blocked_output = false;
    usb_connected = true;
    install_result = ESP_OK;
    input.clear();
    output.clear();
    read_limit = write_limit = 512;
}

template <typename Predicate>
void WaitFor(Predicate ready) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!ready() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    assert(ready());
}

void Send(const std::string& bytes) {
    std::lock_guard<std::mutex> lock(input_mutex);
    input += bytes;
}

bool InputEmpty() {
    std::lock_guard<std::mutex> lock(input_mutex);
    return input.empty();
}

void WaitForOutput(const std::string& text) {
    WaitFor([&] {
        std::lock_guard<std::mutex> lock(output_mutex);
        return output.find(text) != std::string::npos;
    });
}

class RecordingExecutor final : public CliExecutor {
public:
    ExecuteStatus Execute(const Invocation& invocation, BoundedOutput* result) override {
        // Tests submit one fresh command after all retired input is drained.
        assert(invocation.count == 1 && std::strcmp(invocation[0], "fresh") == 0);
        ++calls;
        result->Append("fresh reply");
        return ExecuteStatus::kOk;
    }
    void Cancel() override { ++cancelled; }
    std::atomic<unsigned> calls{0}, cancelled{0};
};

class PendingExecutor final : public CliExecutor {
public:
    ExecuteStatus Execute(const Invocation&, BoundedOutput*) override {
        started = true;
        return ExecuteStatus::kPending;
    }
    ExecuteStatus Poll(BoundedOutput*) override { return ExecuteStatus::kPending; }
    void Cancel() override { ++cancelled; }
    std::atomic<bool> started{false};
    std::atomic<unsigned> cancelled{0};
};

void TestStopDuringTaskExit() {
    Reset();
    CliUsbService service;
    PendingExecutor executor;
    assert(service.Start(&executor) == ESP_OK && service.running());
    assert(service.Start(&executor) == ESP_ERR_INVALID_STATE);
    {
        std::lock_guard<std::mutex> lock(input_mutex);
        input = "pending\r";
    }
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!executor.started && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    assert(executor.started);
    blocked_output = true;
    // Schedule the worker's timed wake before a stop notification can arrive.
    delay_notification = true;
    service.Stop();
    assert(!service.running() && executor.cancelled == 1);
    service.Stop();
    Reset();
    assert(service.Start() == ESP_OK && service.running());
    service.Stop();
    Reset();
}

void TestStartupFailures() {
    for (unsigned failure = 1; failure <= 4; ++failure) {
        Reset();
        CliUsbService service;
        if (failure <= 2) fail_semaphore_at = failure;
        if (failure == 3) fail_task = true;
        if (failure == 4) install_result = ESP_FAIL;
        assert(service.Start() == (failure == 4 ? ESP_FAIL : ESP_ERR_NO_MEM));
        assert(!service.running());
        service.Stop();
        Reset();
        assert(service.Start() == ESP_OK);
        service.Stop();
        Reset();
    }
}

void TestDisconnectDropsBufferedInput() {
    Reset();
    read_limit = 7;
    write_limit = 5;
    CliUsbService service;
    RecordingExecutor executor;
    assert(service.Start(&executor) == ESP_OK);
    WaitForOutput("zectrix> ");
    Send("partial");
    WaitForOutput("partial");
    {
        std::lock_guard<std::mutex> lock(input_mutex);
        usb_connected = false;
        input = std::string(480, 'x') + "stale\r";
    }
    WaitFor([&] { return executor.cancelled != 0 && InputEmpty(); });
    assert(executor.calls == 0);
    // Input queued after the first disconnect poll must also be retired.
    Send("stale\r");
    WaitFor(InputEmpty);
    {
        std::lock_guard<std::mutex> lock(output_mutex);
        output.clear();
    }
    usb_connected = true;
    WaitForOutput("Zectrix maintenance CLI\r\nzectrix> ");
    Send("fresh\r");
    WaitForOutput("fresh reply\r\nzectrix> ");
    assert(executor.calls == 1);
    service.Stop();
    Reset();
}

void TestTxOverflowCannotExecuteBufferedTail() {
    Reset();
    read_limit = 7;
    write_limit = 5;
    CliUsbService service;
    RecordingExecutor executor;
    assert(service.Start(&executor) == ESP_OK);
    WaitForOutput("zectrix> ");
    Send(std::string(180, 'a'));
    WaitForOutput(std::string(180, 'a'));
    blocked_output = true;
    {
        std::lock_guard<std::mutex> lock(output_mutex);
        output.clear();
    }
    // Mid-line inserts redraw enough text to fill the bounded pending TX queue.
    Send("\x1b[H" + std::string(20, 'b') + "stale\r");
    WaitFor([&] { return executor.cancelled != 0 && InputEmpty(); });
    assert(executor.calls == 0);
    blocked_output = false;
    WaitForOutput("Zectrix maintenance CLI\r\nzectrix> ");
    Send("fresh\r");
    WaitForOutput("fresh reply\r\nzectrix> ");
    assert(executor.calls == 1);
    service.Stop();
    Reset();
}
}  // namespace

SemaphoreHandle_t xSemaphoreCreateBinary() {
    if (++semaphore_calls == fail_semaphore_at) return nullptr;
    ++semaphores;
    return new CliHostSemaphore;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks) {
    std::unique_lock<std::mutex> lock(semaphore->mutex);
    const auto ready = [&] { return semaphore->ready; };
    if (ticks == portMAX_DELAY) semaphore->changed.wait(lock, ready);
    else if (!semaphore->changed.wait_for(lock, std::chrono::milliseconds(ticks), ready)) return pdFALSE;
    semaphore->ready = false;
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    std::lock_guard<std::mutex> lock(semaphore->mutex);
    semaphore->ready = true;
    semaphore->changed.notify_one();
    return pdTRUE;
}
void vSemaphoreDelete(SemaphoreHandle_t semaphore) { --semaphores; delete semaphore; }

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t function, const char*, uint32_t,
                                  void* context, unsigned, TaskHandle_t* output, BaseType_t) {
    if (fail_task) return pdFALSE;
    auto task = std::make_unique<CliHostTask>();
    const auto handle = task.get();
    if (output != nullptr) *output = handle;
    task->worker = std::thread([=] {
        current_task = handle;
        try { function(context); } catch (const TaskExit&) {}
    });
    tasks.push_back(std::move(task));
    return pdPASS;
}
BaseType_t xPortGetCoreID() { return 0; }
void xTaskNotifyGive(TaskHandle_t task) {
    std::unique_lock<std::mutex> lock(task->mutex);
    if (delay_notification) {
        assert(task->changed.wait_for(lock, 2s, [&] { return task->deleted; }));
    }
    if (task->deleted) { ++stale_notifications; return; }
    task->notified = true;
    task->changed.notify_all();
}
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t ticks) {
    std::unique_lock<std::mutex> lock(current_task->mutex);
    current_task->changed.wait_for(lock, std::chrono::milliseconds(ticks),
                                  [] { return current_task->notified; });
    current_task->notified = false;
    return 0;
}
void vTaskDelay(TickType_t ticks) { std::this_thread::sleep_for(std::chrono::milliseconds(ticks)); }
void vTaskDelete(TaskHandle_t task) {
    assert(task == nullptr);
    {
        std::lock_guard<std::mutex> lock(current_task->mutex);
        current_task->deleted = true;
        current_task->changed.notify_all();
    }
    throw TaskExit{};
}

esp_err_t usb_serial_jtag_driver_install(const usb_serial_jtag_driver_config_t* config) {
    assert(config->rx_buffer_size == 512 && config->tx_buffer_size == 512);
    assert(!installed.exchange(install_result == ESP_OK));
    return install_result;
}
esp_err_t usb_serial_jtag_driver_uninstall() {
    assert(!capturing && !vfs_driver && installed.exchange(false));
    return ESP_OK;
}
bool usb_serial_jtag_is_connected() { return usb_connected; }
int usb_serial_jtag_read_bytes(void* destination, uint32_t capacity, TickType_t ticks) {
    assert(installed && ticks == 0);
    std::lock_guard<std::mutex> lock(input_mutex);
    const auto count = std::min({static_cast<std::size_t>(capacity), input.size(), read_limit});
    std::memcpy(destination, input.data(), count);
    input.erase(0, count);
    return static_cast<int>(count);
}
std::size_t usb_serial_jtag_get_read_bytes_available() {
    assert(installed);
    std::lock_guard<std::mutex> lock(input_mutex);
    return input.size();
}
int usb_serial_jtag_write_bytes(const void* data, std::size_t size, TickType_t ticks) {
    assert(installed && ticks == 0);
    if (blocked_output || !usb_connected) return 0;
    const auto count = std::min(size, write_limit);
    std::lock_guard<std::mutex> lock(output_mutex);
    output.append(static_cast<const char*>(data), count);
    return static_cast<int>(count);
}
esp_err_t usb_serial_jtag_wait_tx_done(TickType_t ticks) {
    assert(installed && ticks <= 100);
    return ESP_OK;
}
void usb_serial_jtag_vfs_use_driver() { assert(installed && !vfs_driver.exchange(true)); }
void usb_serial_jtag_vfs_use_nonblocking() { assert(installed && vfs_driver.exchange(false)); }
void zectrix::cli::StartMaintenanceLogCapture() { assert(vfs_driver && !capturing.exchange(true)); }
void zectrix::cli::StopMaintenanceLogCapture() { assert(capturing.exchange(false)); }

int main() {
    TestStopDuringTaskExit();
    TestStartupFailures();
    TestDisconnectDropsBufferedInput();
    TestTxOverflowCannotExecuteBufferedTail();
    Reset();
}
