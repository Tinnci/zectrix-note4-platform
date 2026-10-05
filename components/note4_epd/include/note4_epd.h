#ifndef NOTE4_EPD_H_
#define NOTE4_EPD_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "note4_epd_calibration.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NOTE4_EPD_PANEL_WIDTH 400
#define NOTE4_EPD_PANEL_HEIGHT 300
#define NOTE4_EPD_1BPP_FRAME_BYTES 15000
#define NOTE4_EPD_2BPP_FRAME_BYTES 30000
#define NOTE4_EPD_4BPP_FRAME_BYTES 60000
#define NOTE4_EPD_TILE_COLUMNS 5
#define NOTE4_EPD_TILE_ROWS 4
#define NOTE4_EPD_TILE_COUNT (NOTE4_EPD_TILE_COLUMNS * NOTE4_EPD_TILE_ROWS)

/** Opaque driver instance. All public operations are synchronous. */
typedef struct note4_epd_t* note4_epd_handle_t;

typedef struct {
    int x;
    int y;
    int width;
    int height;
} note4_epd_rect_t;

typedef struct {
    uint16_t black_to_white;
    uint16_t white_to_black;
} note4_epd_transitions_t;

typedef struct {
    note4_epd_rect_t dirty;
    uint32_t changed_pixels;
    note4_epd_transitions_t tiles[NOTE4_EPD_TILE_COUNT];
} note4_epd_diff_t;

/** Cumulative completed SPI bytes and observed BUSY waits; no extra I/O. */
typedef struct {
    uint64_t spi_bytes;
    uint64_t ram_bytes;
    uint64_t busy_us;
    uint64_t refresh_busy_us;
    uint32_t refresh_triggers;
    int16_t temperature_centi_c;
    int64_t temperature_sampled_us;  /**< -1 when unavailable. */
} note4_epd_metrics_t;

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
} note4_epd_config_t;

/** Fill config with the pinout used by zectrix-s3-epaper-4.2. */
void note4_epd_get_default_config(note4_epd_config_t* config);

/** Allocate the driver and configure GPIO/SPI. The panel power stays off. */
esp_err_t note4_epd_new(const note4_epd_config_t* config,
                          note4_epd_handle_t* out_handle);

/** Release the driver. Powers the panel off first when necessary. */
esp_err_t note4_epd_del(note4_epd_handle_t handle);

/** Enable the external rail, reset SSD2683 and select its OTP waveform. */
esp_err_t note4_epd_power_on(note4_epd_handle_t handle);

/** Power down SSD2683 and disable the external panel rail. */
esp_err_t note4_epd_power_off(note4_epd_handle_t handle);

/** True after power_on and before power_off. */
bool note4_epd_is_powered(note4_epd_handle_t handle);

/** Copy active calibration without panel I/O. */
esp_err_t note4_epd_read_calibration(note4_epd_handle_t handle,
                                     note4_epd_calibration_t* calibration);
/** Replace validated calibration only while the external rail is off.
 * No persistence here; the platform owns saving and boot-time restoration.
 */
esp_err_t note4_epd_set_calibration(note4_epd_handle_t handle,
                                    const note4_epd_calibration_t* calibration);

/** Copy counters under the existing driver mutex without accessing the panel. */
esp_err_t note4_epd_read_metrics(note4_epd_handle_t handle, note4_epd_metrics_t* metrics);

/** Copy a bounded range of the existing 1bpp shadow without powering the panel. */
esp_err_t note4_epd_copy_shadow(note4_epd_handle_t handle, size_t offset,
                                  uint8_t* destination, size_t size);

/**
 * Find the smallest pixel rectangle that differs from the valid 1bpp shadow.
 *
 * rect locates a tightly packed MSB-first patch, or the entire 400x300 frame.
 * Unused bits at the end of each source row are ignored. An unchanged image
 * returns a zero rectangle. This operation does not power or modify the panel.
 */
esp_err_t note4_epd_find_dirty_1bpp(note4_epd_handle_t handle,
                                      const note4_epd_rect_t* rect,
                                      const uint8_t* pixels, size_t pixels_size,
                                      note4_epd_rect_t* dirty);

/**
 * Find changed bounds and count actual black/white pixel transitions.
 *
 * Uses the same packed input and valid shadow as find_dirty_1bpp. Padding
 * bits and unchanged pixels inside the bounds do not contribute to the count.
 * This query allocates no memory and does not power or modify the panel.
 * The result is cleared on error, including an invalid 1bpp shadow.
 */
esp_err_t note4_epd_analyze_1bpp(note4_epd_handle_t handle,
                                  const note4_epd_rect_t* rect,
                                  const uint8_t* pixels, size_t pixels_size,
                                  note4_epd_diff_t* result);

/**
 * Full-screen black/white refresh.
 *
 * Format: 400x300, row-major, MSB first, 1=white and 0=black.
 * The buffer must contain exactly NOTE4_EPD_1BPP_FRAME_BYTES bytes.
 */
esp_err_t note4_epd_refresh_full_1bpp(note4_epd_handle_t handle,
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
esp_err_t note4_epd_refresh_partial_1bpp(note4_epd_handle_t handle,
                                           const note4_epd_rect_t* rect,
                                           const uint8_t* pixels,
                                           size_t pixels_size);

/**
 * Full-screen 16-code gray refresh using the SSD2683 vendor-record sequence.
 * Actual optical separation requires panel-specific measurement/calibration.
 *
 * Format: 400x300, row-major, two pixels per byte, left pixel in the high
 * nibble, 0=black and 15=white. Partial refresh is intentionally unsupported
 * after this call until another full 1bpp refresh establishes a B/W base.
 */
esp_err_t note4_epd_refresh_full_4bpp(note4_epd_handle_t handle,
                                        const uint8_t* framebuffer,
                                        size_t framebuffer_size);
/** Full-screen four-tone input, four MSB-first pixels per byte, 0=black,
 * 3=white. Interior tones use the active optical profile at 333/667 permille.
 * Uses the same OTP-white + five-pass transaction, not a faster waveform.
 * No full 4bpp expansion buffer and no grayscale partial refresh.
 */
esp_err_t note4_epd_refresh_full_2bpp(note4_epd_handle_t handle, const uint8_t* framebuffer,
                                      size_t framebuffer_size);

#ifdef __cplusplus
}
#endif

#endif  // NOTE4_EPD_H_
