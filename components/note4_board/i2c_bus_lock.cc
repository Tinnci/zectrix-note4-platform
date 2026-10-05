#include "i2c_bus_lock.h"

#include "note4_log_event.h"
#include <freertos/semphr.h>

namespace {

constexpr char kTag[] = "i2c_lock";

SemaphoreHandle_t GetI2cBusMutex() {
    static SemaphoreHandle_t mutex = []() {
        SemaphoreHandle_t created = xSemaphoreCreateRecursiveMutex();
        configASSERT(created != nullptr);
        return created;
    }();
    return mutex;
}

}  // namespace

esp_err_t LockI2cBus(const char* owner, TickType_t timeout_ticks) {
    if (xSemaphoreTakeRecursive(GetI2cBusMutex(), timeout_ticks) == pdTRUE) {
        return ESP_OK;
    }
    NOTE4_LOGW(kTag, "i2c_lock_timeout", "owner=%s timeout_ticks=%lu",
               note4::log::Token(owner ? owner : "unknown").c_str(),
               static_cast<unsigned long>(timeout_ticks));
    return ESP_ERR_TIMEOUT;
}

void UnlockI2cBus() {
    xSemaphoreGiveRecursive(GetI2cBusMutex());
}

ScopedI2cBusLock::ScopedI2cBusLock(const char* owner, TickType_t timeout_ticks) {
    status_ = LockI2cBus(owner, timeout_ticks);
}

ScopedI2cBusLock::~ScopedI2cBusLock() {
    if (status_ == ESP_OK) {
        UnlockI2cBus();
    }
}
