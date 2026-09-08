#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

struct usb_serial_jtag_driver_config_t { uint32_t tx_buffer_size, rx_buffer_size; };
#define USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT() usb_serial_jtag_driver_config_t{512, 512}
esp_err_t usb_serial_jtag_driver_install(const usb_serial_jtag_driver_config_t*);
esp_err_t usb_serial_jtag_driver_uninstall();
bool usb_serial_jtag_is_connected();
int usb_serial_jtag_read_bytes(void*, uint32_t, TickType_t);
std::size_t usb_serial_jtag_get_read_bytes_available();
int usb_serial_jtag_write_bytes(const void*, std::size_t, TickType_t);
esp_err_t usb_serial_jtag_wait_tx_done(TickType_t);
