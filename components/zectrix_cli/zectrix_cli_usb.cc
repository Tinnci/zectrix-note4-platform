#include "zectrix_cli_usb.h"

#include <atomic>
#include <cstring>
#include <new>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace zectrix::cli {
namespace {

constexpr TickType_t kPollTicks = pdMS_TO_TICKS(20);
constexpr TickType_t kWriteTicks = pdMS_TO_TICKS(100);

class UsbSerialJtagTransport final : public CliTransport {
public:
    bool IsConnected() const override {
        return usb_serial_jtag_is_connected();
    }

    std::size_t Read(uint8_t* destination, std::size_t capacity) override {
        if (destination == nullptr || capacity == 0) return 0;
        const int received = usb_serial_jtag_read_bytes(
            destination, static_cast<uint32_t>(capacity), 0);
        return received > 0 ? static_cast<std::size_t>(received) : 0;
    }

    bool Write(const char* data, std::size_t size) override {
        if (data == nullptr) return false;
        std::size_t offset = 0;
        while (offset < size && IsConnected()) {
            const int written = usb_serial_jtag_write_bytes(
                data + offset, size - offset, kWriteTicks);
            if (written <= 0) return false;
            offset += static_cast<std::size_t>(written);
        }
        return offset == size;
    }
};

class BootstrapExecutor final : public CliExecutor {
public:
    ExecuteStatus Execute(const Invocation& invocation,
                          BoundedOutput* output) override {
        if (output == nullptr || invocation.count == 0) {
            return ExecuteStatus::kInvalidArguments;
        }
        if (std::strcmp(invocation[0], "help") == 0) {
            if (invocation.count != 1) return ExecuteStatus::kInvalidArguments;
            output->Append("help     Show commands\r\n");
            output->Append("version  Show CLI version");
            return ExecuteStatus::kOk;
        }
        if (std::strcmp(invocation[0], "version") == 0) {
            if (invocation.count != 1) return ExecuteStatus::kInvalidArguments;
            output->Append("zectrix maintenance CLI D1");
            return ExecuteStatus::kOk;
        }
        return ExecuteStatus::kUnknownCommand;
    }
};

}  // namespace

struct CliUsbService::Impl {
    static void TaskEntry(void* context) {
        static_cast<Impl*>(context)->Run();
    }

    void Run() {
        usb_serial_jtag_driver_config_t config =
            USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        config.tx_buffer_size = 512;
        config.rx_buffer_size = 512;
        start_result.store(usb_serial_jtag_driver_install(&config));
        if (start_result.load() == ESP_OK) {
            // Secondary console output shares the same interrupt-driven
            // driver while the CLI owns USB Serial/JTAG.
            usb_serial_jtag_vfs_use_driver();
        }
        active.store(start_result.load() == ESP_OK);
        xSemaphoreGive(ready);
        if (start_result.load() == ESP_OK) {
            UsbSerialJtagTransport transport;
            CliSession session(transport,
                               executor == nullptr ? bootstrap : *executor);
            while (!stop_requested.load()) {
                session.Poll();
                ulTaskNotifyTake(pdTRUE, kPollTicks);
            }
            session.Reset();
            usb_serial_jtag_wait_tx_done(kWriteTicks);
            usb_serial_jtag_vfs_use_nonblocking();
            usb_serial_jtag_driver_uninstall();
        }
        active.store(false);
        xSemaphoreGive(done);
        vTaskDelete(nullptr);
    }

    BootstrapExecutor bootstrap;
    CliExecutor* executor = nullptr;
    TaskHandle_t task = nullptr;
    SemaphoreHandle_t ready = nullptr;
    SemaphoreHandle_t done = nullptr;
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> active{false};
    std::atomic<esp_err_t> start_result{ESP_FAIL};
};

CliUsbService::CliUsbService() : impl_(new (std::nothrow) Impl()) {}

CliUsbService::~CliUsbService() {
    Stop();
    delete impl_;
}

esp_err_t CliUsbService::Start(CliExecutor* executor) {
    if (impl_ == nullptr) return ESP_ERR_NO_MEM;
    if (impl_->task != nullptr) return ESP_ERR_INVALID_STATE;
    impl_->ready = xSemaphoreCreateBinary();
    impl_->done = xSemaphoreCreateBinary();
    if (impl_->ready == nullptr || impl_->done == nullptr) {
        if (impl_->ready != nullptr) vSemaphoreDelete(impl_->ready);
        if (impl_->done != nullptr) vSemaphoreDelete(impl_->done);
        impl_->ready = nullptr;
        impl_->done = nullptr;
        return ESP_ERR_NO_MEM;
    }
    impl_->executor = executor;
    impl_->stop_requested.store(false);
    impl_->start_result.store(ESP_FAIL);
    const BaseType_t created = xTaskCreatePinnedToCore(
        &Impl::TaskEntry, "zectrix_cli", 4096, impl_, 3, &impl_->task,
        xPortGetCoreID());
    if (created != pdPASS) {
        vSemaphoreDelete(impl_->ready);
        vSemaphoreDelete(impl_->done);
        impl_->ready = nullptr;
        impl_->done = nullptr;
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(impl_->ready, portMAX_DELAY);
    if (impl_->start_result.load() != ESP_OK) {
        xSemaphoreTake(impl_->done, portMAX_DELAY);
        vSemaphoreDelete(impl_->ready);
        vSemaphoreDelete(impl_->done);
        impl_->ready = nullptr;
        impl_->done = nullptr;
        impl_->task = nullptr;
        return impl_->start_result.load();
    }
    return ESP_OK;
}

void CliUsbService::Stop() {
    if (impl_ == nullptr || impl_->done == nullptr) return;
    impl_->stop_requested.store(true);
    const TaskHandle_t task = impl_->task;
    if (task != nullptr) xTaskNotifyGive(task);
    xSemaphoreTake(impl_->done, portMAX_DELAY);
    vSemaphoreDelete(impl_->ready);
    vSemaphoreDelete(impl_->done);
    impl_->ready = nullptr;
    impl_->done = nullptr;
    impl_->executor = nullptr;
    impl_->task = nullptr;
}

bool CliUsbService::running() const {
    return impl_ != nullptr && impl_->active.load();
}

}  // namespace zectrix::cli
