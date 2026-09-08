#pragma once

#include "esp_err.h"

namespace zectrix::cli {

class CliExecutor;

class CliUsbService final {
public:
    CliUsbService();
    ~CliUsbService();

    esp_err_t Start(CliExecutor* executor = nullptr);
    void Stop();
    bool running() const;
};

}  // namespace zectrix::cli
