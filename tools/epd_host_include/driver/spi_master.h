#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

using spi_host_device_t = int;
constexpr spi_host_device_t SPI3_HOST = 3;
constexpr int SPI_DMA_CH_AUTO = 3;
constexpr uint32_t SPI_TRANS_USE_TXDATA = 1;
constexpr uint32_t SPI_TRANS_USE_RXDATA = 2;
struct FakeSpiDevice;
using spi_device_handle_t = FakeSpiDevice*;
struct spi_bus_config_t {
    int miso_io_num, mosi_io_num, sclk_io_num, quadwp_io_num, quadhd_io_num;
    std::size_t max_transfer_sz;
};
struct spi_device_interface_config_t { int spics_io_num, clock_speed_hz, mode, queue_size; };
struct spi_transaction_t {
    std::size_t length;
    uint32_t flags;
    uint8_t tx_data[4], rx_data[4];
    const void* tx_buffer;
};
esp_err_t spi_bus_initialize(spi_host_device_t, const spi_bus_config_t*, int);
esp_err_t spi_bus_free(spi_host_device_t);
esp_err_t spi_bus_add_device(spi_host_device_t, const spi_device_interface_config_t*, spi_device_handle_t*);
esp_err_t spi_bus_remove_device(spi_device_handle_t);
esp_err_t spi_device_polling_transmit(spi_device_handle_t, spi_transaction_t*);
