#pragma once

#include "esp_err.h"

class ZectrixSelfTest;

namespace zectrix::display { class DisplayService; }
namespace zectrix::input { class InputService; }
namespace zectrix::power { class PowerService; }
namespace zectrix::storage { class StorageService; }
namespace zectrix::system { class SystemService; }
namespace zectrix::time { class TimeService; }

namespace zectrix {

class Platform {
public:
    Platform() = default;
    ~Platform();

    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;

    esp_err_t Initialize();
    bool IsInitialized() const { return initialized_; }

    display::DisplayService& Display() const;
    input::InputService& Input() const;
    power::PowerService& Power() const;
    time::TimeService& Time() const;
    storage::StorageService& Storage() const;
    system::SystemService& System() const;
    ZectrixSelfTest& Diagnostics() const;

private:
    void ResetServices();
    struct Impl;
    Impl* impl_ = nullptr;
    bool initialization_attempted_ = false;
    bool initialized_ = false;
};

}  // namespace zectrix
