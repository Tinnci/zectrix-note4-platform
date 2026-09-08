#include "zectrix_cli_usb.h"

#include <atomic>
#include <algorithm>
#include <cstring>
#include <new>
#include <optional>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "zectrix_cli_log.h"

namespace zectrix::cli {
namespace {

constexpr TickType_t kPollTicks = pdMS_TO_TICKS(20);
constexpr TickType_t kWriteTicks = pdMS_TO_TICKS(100);
constexpr std::size_t kRxBufferSize = 512;

class UsbSerialJtagTransport final : public CliTransport {
public:
    bool IsConnected() const override {
        return usb_serial_jtag_is_connected();
    }

    std::size_t Read(uint8_t* destination, std::size_t capacity) override {
        if (destination == nullptr || capacity == 0 || !IsConnected()) return 0;
        const int received = usb_serial_jtag_read_bytes(
            destination, static_cast<uint32_t>(capacity), 0);
        return received > 0 ? static_cast<std::size_t>(received) : 0;
    }

    bool Write(const char* data, std::size_t size) override {
        if (data == nullptr) return false;
        Flush();
        if (!IsConnected() || size > pending_.size() - count_) {
            Reset();
            return false;
        }
        for (std::size_t index = 0; index < size; ++index) {
            pending_[(head_ + count_++) % pending_.size()] = data[index];
        }
        return true;
    }

    void Flush() {
        if (!IsConnected()) {
            Reset();
            DiscardInput();
            return;
        }
        if (count_ == 0) return;
        const std::size_t size = std::min(count_, pending_.size() - head_);
        const int sent = usb_serial_jtag_write_bytes(pending_.data() + head_, size, 0);
        if (sent <= 0) return;
        const auto consumed = std::min(size, static_cast<std::size_t>(sent));
        head_ = (head_ + consumed) % pending_.size();
        count_ -= consumed;
    }

    void Reset() { head_ = count_ = 0; }

    void DiscardInput() override {
        // Drain only the queued snapshot, including a wrapped ring-buffer tail.
        auto remaining = std::min(kRxBufferSize, usb_serial_jtag_get_read_bytes_available());
        std::array<uint8_t, 64> discarded{};
        while (remaining != 0) {
            const int received = usb_serial_jtag_read_bytes(
                discarded.data(), std::min(remaining, discarded.size()), 0);
            if (received <= 0) break;
            remaining -= std::min(remaining, static_cast<std::size_t>(received));
        }
    }

private:
    std::array<char, 2048> pending_{};
    std::size_t head_ = 0;
    std::size_t count_ = 0;
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
        config.rx_buffer_size = kRxBufferSize;
        start_result.store(usb_serial_jtag_driver_install(&config));
        if (start_result.load() == ESP_OK) {
            // Secondary console output shares the same interrupt-driven
            // driver while the CLI owns USB Serial/JTAG.
            usb_serial_jtag_vfs_use_driver();
            StartMaintenanceLogCapture();
        }
        active.store(start_result.load() == ESP_OK);
        xSemaphoreGive(ready);
        if (start_result.load() == ESP_OK) {
            while (!stop_requested.load()) {
                transport.Flush();
                session->Poll();
                transport.Flush();
                vTaskDelay(kPollTicks);
            }
            session->Reset();
            StopMaintenanceLogCapture();
            usb_serial_jtag_wait_tx_done(kWriteTicks);
            usb_serial_jtag_vfs_use_nonblocking();
            usb_serial_jtag_driver_uninstall();
        }
        active.store(false);
        xSemaphoreGive(done);
        vTaskDelete(nullptr);
    }

    BootstrapExecutor bootstrap;
    // Editing/history and pending TX storage must not consume the CLI stack.
    UsbSerialJtagTransport transport;
    std::optional<CliSession> session;
    CliExecutor* executor = nullptr;
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
    if (impl_->done != nullptr) return ESP_ERR_INVALID_STATE;
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
    impl_->transport.Reset();
    impl_->session.emplace(impl_->transport,
                           executor == nullptr ? impl_->bootstrap : *executor);
    impl_->stop_requested.store(false);
    impl_->start_result.store(ESP_FAIL);
    const BaseType_t created = xTaskCreatePinnedToCore(
        &Impl::TaskEntry, "zectrix_cli", 4096, impl_, 3, nullptr,
        xPortGetCoreID());
    if (created != pdPASS) {
        vSemaphoreDelete(impl_->ready);
        vSemaphoreDelete(impl_->done);
        impl_->ready = nullptr;
        impl_->done = nullptr;
        impl_->session.reset();
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(impl_->ready, portMAX_DELAY);
    if (impl_->start_result.load() != ESP_OK) {
        xSemaphoreTake(impl_->done, portMAX_DELAY);
        vSemaphoreDelete(impl_->ready);
        vSemaphoreDelete(impl_->done);
        impl_->ready = nullptr;
        impl_->done = nullptr;
        impl_->session.reset();
        return impl_->start_result.load();
    }
    return ESP_OK;
}

void CliUsbService::Stop() {
    if (impl_ == nullptr || impl_->done == nullptr) return;
    impl_->stop_requested.store(true);
    // The worker can self-delete as soon as it observes the flag. Its bounded
    // poll delay needs no wake notification through a potentially stale TCB.
    xSemaphoreTake(impl_->done, portMAX_DELAY);
    vSemaphoreDelete(impl_->ready);
    vSemaphoreDelete(impl_->done);
    impl_->ready = nullptr;
    impl_->done = nullptr;
    impl_->executor = nullptr;
    impl_->session.reset();
}

bool CliUsbService::running() const {
    return impl_ != nullptr && impl_->active.load();
}

}  // namespace zectrix::cli
