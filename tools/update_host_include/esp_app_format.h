#pragma once

#include <cstdint>

enum esp_chip_id_t : uint16_t { ESP_CHIP_ID_ESP32 = 0, ESP_CHIP_ID_ESP32S3 = 9 };
enum esp_image_spi_mode_t { ESP_IMAGE_SPI_MODE_QIO, ESP_IMAGE_SPI_MODE_QOUT,
    ESP_IMAGE_SPI_MODE_DIO, ESP_IMAGE_SPI_MODE_DOUT, ESP_IMAGE_SPI_MODE_FAST_READ,
    ESP_IMAGE_SPI_MODE_SLOW_READ };
enum esp_image_spi_freq_t { ESP_IMAGE_SPI_SPEED_DIV_2, ESP_IMAGE_SPI_SPEED_DIV_3,
    ESP_IMAGE_SPI_SPEED_DIV_4, ESP_IMAGE_SPI_SPEED_DIV_1 = 15 };
constexpr uint8_t ESP_IMAGE_FLASH_SIZE_MAX = 8;
constexpr uint8_t ESP_IMAGE_HEADER_MAGIC = 0xe9;
constexpr uint8_t ESP_IMAGE_MAX_SEGMENTS = 16;

struct __attribute__((packed)) esp_image_header_t {
    uint8_t magic;
    uint8_t segment_count;
    uint8_t spi_mode;
    uint8_t spi_speed : 4;
    uint8_t spi_size : 4;
    uint32_t entry_addr;
    uint8_t wp_pin;
    uint8_t spi_pin_drv[3];
    esp_chip_id_t chip_id;
    uint8_t min_chip_rev;
    uint16_t min_chip_rev_full;
    uint16_t max_chip_rev_full;
    uint8_t reserved[4];
    uint8_t hash_appended;
};
struct esp_image_segment_header_t { uint32_t load_addr; uint32_t data_len; };
