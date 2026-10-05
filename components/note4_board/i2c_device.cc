#include "i2c_device.h"

#include "note4_log_event.h"

#include <cstring>

#include "i2c_bus_lock.h"

#define TAG "I2cDevice"

extern "C" void __attribute__((weak)) BoardI2cForcePowerOn() {}

constexpr int kI2cTimeoutMs = 100;

I2cDevice::I2cDevice(i2c_master_bus_handle_t i2c_bus, uint8_t addr)
    : i2c_bus_(i2c_bus), device_address_(addr) {
    ScopedI2cBusLock bus_lock("I2cDevice::I2cDevice");
    if (!bus_lock.locked()) {
        initialization_status_ = bus_lock.status();
        return;
    }
    i2c_device_config_t i2c_device_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400 * 1000,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = 0,
        },
    };
    initialization_status_ =
        i2c_master_bus_add_device(i2c_bus, &i2c_device_cfg, &i2c_device_);
    if (initialization_status_ == ESP_OK && i2c_device_ == nullptr) {
        initialization_status_ = ESP_ERR_INVALID_STATE;
    }
}

I2cDevice::~I2cDevice() {
    if (initialization_status_ == ESP_OK && i2c_device_ != nullptr) {
        i2c_master_bus_rm_device(i2c_device_);
        i2c_device_ = nullptr;
    }
}

esp_err_t I2cDevice::ResetBus(const char* reason) {
    if (initialization_status_ != ESP_OK) return initialization_status_;
    ScopedI2cBusLock bus_lock("I2cDevice::ResetBus");
    if (!bus_lock.locked()) {
        return bus_lock.status();
    }
    NOTE4_LOGW(TAG, "i2c_reset_start", "reason=%s address=0x%02X",
               note4::log::Token(reason ? reason : "unknown").c_str(),
               static_cast<unsigned>(device_address_));
    esp_err_t ret = i2c_master_bus_reset(i2c_bus_);
    NOTE4_LOGW(TAG, "i2c_reset_done", "result=%s", note4::log::Token(esp_err_to_name(ret)).c_str());
    return ret;
}

void I2cDevice::WriteReg(uint8_t reg, uint8_t value) {
    esp_err_t ret = WriteRegChecked(reg, value);
    ESP_ERROR_CHECK(ret);
}

esp_err_t I2cDevice::WriteRegChecked(uint8_t reg, uint8_t value) {
    return WriteRegsChecked(reg, &value, 1);
}

esp_err_t I2cDevice::WriteRegsChecked(uint8_t reg, const uint8_t* values,
                                      size_t length) {
    constexpr size_t kMaxRegisterWriteBytes = 32;
    if (values == nullptr || length == 0 || length > kMaxRegisterWriteBytes) {
        return ESP_ERR_INVALID_ARG;
    }
    if (initialization_status_ != ESP_OK) return initialization_status_;
    ScopedI2cBusLock bus_lock("I2cDevice::WriteReg");
    if (!bus_lock.locked()) {
        return bus_lock.status();
    }
    uint8_t buffer[kMaxRegisterWriteBytes + 1] = {};
    buffer[0] = reg;
    memcpy(buffer + 1, values, length);
    BoardI2cForcePowerOn();
    esp_err_t ret = i2c_master_transmit(i2c_device_, buffer, length + 1,
                                        kI2cTimeoutMs);
    if (ret == ESP_ERR_INVALID_STATE || ret == ESP_ERR_TIMEOUT) {
        NOTE4_LOGW(TAG, "i2c_write_failed", "addr=0x%02X reg=0x%02X len=%u ret=%s",
                   static_cast<unsigned>(device_address_), static_cast<unsigned>(reg),
                   static_cast<unsigned>(length), note4::log::Token(esp_err_to_name(ret)).c_str());
        if (ResetBus("write_retry") == ESP_OK) {
            BoardI2cForcePowerOn();
            ret = i2c_master_transmit(i2c_device_, buffer, length + 1,
                                      kI2cTimeoutMs);
            NOTE4_LOGW(TAG, "i2c_write_retry_result", "addr=0x%02X reg=0x%02X len=%u ret=%s",
                       static_cast<unsigned>(device_address_), static_cast<unsigned>(reg),
                       static_cast<unsigned>(length),
                       note4::log::Token(esp_err_to_name(ret)).c_str());
        }
    }
    return ret;
}

uint8_t I2cDevice::ReadReg(uint8_t reg) {
    uint8_t buffer[1] = {0};
    esp_err_t ret = ReadRegChecked(reg, buffer);
    ESP_ERROR_CHECK(ret);
    return buffer[0];
}

void I2cDevice::ReadRegs(uint8_t reg, uint8_t* buffer, size_t length) {
    esp_err_t ret = ReadRegsChecked(reg, buffer, length);
    ESP_ERROR_CHECK(ret);
}

esp_err_t I2cDevice::ReadRegChecked(uint8_t reg, uint8_t* value) {
    if (value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    return ReadRegsChecked(reg, value, 1);
}

esp_err_t I2cDevice::ReadRegsChecked(uint8_t reg, uint8_t* buffer, size_t length) {
    if (buffer == nullptr || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (initialization_status_ != ESP_OK) return initialization_status_;

    ScopedI2cBusLock bus_lock("I2cDevice::ReadRegs");
    if (!bus_lock.locked()) {
        return bus_lock.status();
    }
    BoardI2cForcePowerOn();
    esp_err_t ret = i2c_master_transmit_receive(i2c_device_, &reg, 1, buffer, length, 100);
    if (ret == ESP_ERR_INVALID_STATE) {
        NOTE4_LOGW(TAG, "i2c_read_invalid_state", "addr=0x%02X reg=0x%02X len=%u ret=%s",
                   static_cast<unsigned>(device_address_), static_cast<unsigned>(reg),
                   static_cast<unsigned>(length), note4::log::Token(esp_err_to_name(ret)).c_str());
        if (ResetBus("read_invalid_state") == ESP_OK) {
            BoardI2cForcePowerOn();
            ret = i2c_master_transmit_receive(i2c_device_, &reg, 1, buffer, length, 100);
            NOTE4_LOGW(TAG, "i2c_read_retry_result", "addr=0x%02X reg=0x%02X len=%u ret=%s",
                       static_cast<unsigned>(device_address_), static_cast<unsigned>(reg),
                       static_cast<unsigned>(length),
                       note4::log::Token(esp_err_to_name(ret)).c_str());
        }
    }
    return ret;
}
