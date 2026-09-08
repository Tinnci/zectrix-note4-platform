#ifndef ZECTRIX_EPD_H_
#define ZECTRIX_EPD_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZECTRIX_EPD_PANEL_WIDTH 400
#define ZECTRIX_EPD_PANEL_HEIGHT 300
#define ZECTRIX_EPD_1BPP_FRAME_BYTES 15000
#define ZECTRIX_EPD_4BPP_FRAME_BYTES 60000

/** Opaque driver instance. All public operations are synchronous. */
typedef struct zectrix_epd_t* zectrix_epd_handle_t;

typedef struct {
    int x;
    int y;
    int width;
    int height;
} zectrix_epd_rect_t;

typedef struct {
    spi_host_device_t spi_host;
    gpio_num_t pin_cs;
    gpio_num_t pin_dc;
    gpio_num_t pin_reset;
    gpio_num_t pin_busy;
    gpio_num_t pin_mosi;
    gpio_num_t pin_sclk;
    gpio_num_t pin_power;
    int spi_clock_hz;
    uint32_t busy_timeout_ms;
    bool initialize_spi_bus;
} zectrix_epd_config_t;

/** Fill config with the pinout used by zectrix-s3-epaper-4.2. */
void zectrix_epd_get_default_config(zectrix_epd_config_t* config);

/** Allocate the driver and configure GPIO/SPI. The panel power stays off. */
esp_err_t zectrix_epd_new(const zectrix_epd_config_t* config,
                          zectrix_epd_handle_t* out_handle);

/** Release the driver. Powers the panel off first when necessary. */
esp_err_t zectrix_epd_del(zectrix_epd_handle_t handle);

/** Enable the external rail, reset SSD2683 and select its OTP waveform. */
esp_err_t zectrix_epd_power_on(zectrix_epd_handle_t handle);

/** Power down SSD2683 and disable the external panel rail. */
esp_err_t zectrix_epd_power_off(zectrix_epd_handle_t handle);

/** True after power_on and before power_off. */
bool zectrix_epd_is_powered(zectrix_epd_handle_t handle);

/** Copy a bounded range of the existing 1bpp shadow without powering the panel. */
esp_err_t zectrix_epd_copy_shadow(zectrix_epd_handle_t handle, size_t offset,
                                  uint8_t* destination, size_t size);

/**
 * Find the smallest pixel rectangle that differs from the valid 1bpp shadow.
 *
 * rect locates a tightly packed MSB-first patch, or the entire 400x300 frame.
 * Unused bits at the end of each source row are ignored. An unchanged image
 * returns a zero rectangle. This operation does not power or modify the panel.
 */
esp_err_t zectrix_epd_find_dirty_1bpp(zectrix_epd_handle_t handle,
                                      const zectrix_epd_rect_t* rect,
                                      const uint8_t* pixels, size_t pixels_size,
                                      zectrix_epd_rect_t* dirty);

/**
 * Full-screen black/white refresh.
 *
 * Format: 400x300, row-major, MSB first, 1=white and 0=black.
 * The buffer must contain exactly ZECTRIX_EPD_1BPP_FRAME_BYTES bytes.
 */
esp_err_t zectrix_epd_refresh_full_1bpp(zectrix_epd_handle_t handle,
                                        const uint8_t* framebuffer,
                                        size_t framebuffer_size);

/**
 * Partial black/white refresh.
 *
 * pixels contains a tightly packed rect.width x rect.height bitmap using the
 * same MSB-first, 1=white convention as full refresh. Each source row starts
 * at the next byte. A successful full 1bpp refresh must precede partial use.
 * The driver sends only the changed bounding box, with X rounded out to byte
 * boundaries. Unchanged pixels in that window retain their old value. An
 * unchanged patch returns successfully without a controller transaction.
 */
esp_err_t zectrix_epd_refresh_partial_1bpp(zectrix_epd_handle_t handle,
                                           const zectrix_epd_rect_t* rect,
                                           const uint8_t* pixels,
                                           size_t pixels_size);

/**
 * Full-screen 16-gray refresh using the calibrated SSD2683 waveform sequence.
 *
 * Format: 400x300, row-major, two pixels per byte, left pixel in the high
 * nibble, 0=black and 15=white. Partial refresh is intentionally unsupported
 * after this call until another full 1bpp refresh establishes a B/W base.
 */
esp_err_t zectrix_epd_refresh_full_4bpp(zectrix_epd_handle_t handle,
                                        const uint8_t* framebuffer,
                                        size_t framebuffer_size);

#ifdef __cplusplus
}
#endif

#endif  // ZECTRIX_EPD_H_
