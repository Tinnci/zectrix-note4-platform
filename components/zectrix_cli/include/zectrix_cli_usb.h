#pragma once

#include "esp_err.h"
#include "zectrix_cli_session.h"

namespace zectrix::cli {

// Owns the ESP32-S3 USB Serial/JTAG driver and the single CLI session task.
// Start and Stop are application-lifecycle operations, not CLI commands.
class CliUsbService final {
public:
    CliUsbService();
    ~CliUsbService();

    CliUsbService(const CliUsbService&) = delete;
    CliUsbService& operator=(const CliUsbService&) = delete;

    esp_err_t Start(CliExecutor* executor = nullptr);
    void Stop();
    bool running() const;

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace zectrix::cli
