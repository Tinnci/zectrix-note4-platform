#pragma once

#include <cstddef>
#include <cstdint>
#include "driver/gpio.h"
struct BoardHostI2cBus;
struct BoardHostI2cDevice;
using i2c_master_bus_handle_t = BoardHostI2cBus*;
using i2c_master_dev_handle_t = BoardHostI2cDevice*;
enum i2c_port_t { I2C_NUM_0 };
enum { I2C_CLK_SRC_DEFAULT, I2C_ADDR_BIT_LEN_7 };
struct i2c_master_bus_config_t {
    i2c_port_t i2c_port;
    gpio_num_t sda_io_num, scl_io_num;
    int clk_source, glitch_ignore_cnt, intr_priority, trans_queue_depth;
    struct { unsigned enable_internal_pullup; } flags;
};
struct i2c_device_config_t {
    int dev_addr_length;
    uint16_t device_address;
    uint32_t scl_speed_hz, scl_wait_us;
    struct { unsigned disable_ack_check; } flags;
};
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t*, i2c_master_bus_handle_t*);
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t);
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t, const i2c_device_config_t*, i2c_master_dev_handle_t*);
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t);
esp_err_t i2c_master_bus_reset(i2c_master_bus_handle_t);
esp_err_t i2c_master_probe(i2c_master_bus_handle_t, uint16_t, int);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t, const uint8_t*, std::size_t, int);
esp_err_t i2c_master_receive(i2c_master_dev_handle_t, uint8_t*, std::size_t, int);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t, const uint8_t*, std::size_t, uint8_t*, std::size_t, int);
