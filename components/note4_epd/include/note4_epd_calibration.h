#ifndef NOTE4_EPD_CALIBRATION_H_
#define NOTE4_EPD_CALIBRATION_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NOTE4_EPD_CALIBRATION_VERSION 1
#define NOTE4_EPD_CALIBRATION_PANEL 2683
#define NOTE4_EPD_GRAY_LEVELS 16
#define NOTE4_EPD_CALIBRATION_BYTES 92

/** Bounded vendor-record selector, not arbitrary voltage or timing bytes. */
typedef struct {
    uint8_t base_table;
    uint8_t alternate_table;
    uint8_t alternate_mask;
    uint16_t reflectance_permille;
} note4_epd_gray_level_t;

/** Optical values are estimates unless the corresponding measured bit is set.
 * Measurements must refer to this complete five-pass sequence on this panel.
 * White is the OTP base; level 15's selector is the fixed hold recipe.
 */
typedef struct {
    uint16_t version;
    uint16_t panel;
    uint32_t revision;
    uint16_t measured_levels;
    note4_epd_gray_level_t levels[NOTE4_EPD_GRAY_LEVELS];
} note4_epd_calibration_t;

void note4_epd_calibration_default(note4_epd_calibration_t* calibration);
bool note4_epd_calibration_validate(const note4_epd_calibration_t* calibration);
/** Portable little-endian encoding; never serialize native struct padding. */
bool note4_epd_calibration_encode(const note4_epd_calibration_t* calibration, uint8_t* bytes,
                                  size_t size);
/** Failure leaves the destination untouched. */
bool note4_epd_calibration_decode(const uint8_t* bytes, size_t size,
                                  note4_epd_calibration_t* calibration);
/** Transactional level edit. Recipe changes invalidate all optical evidence. */
bool note4_epd_calibration_update(note4_epd_calibration_t* calibration, uint8_t level,
                                  const note4_epd_gray_level_t* value, bool measured);
/** Nearest optical level of an already validated profile, ties toward white.
 * Does not imply LCD gamma. Validate once before processing a frame.
 */
uint8_t note4_epd_calibration_quantize(const note4_epd_calibration_t* calibration,
                                       uint16_t reflectance_permille);

#ifdef __cplusplus
}
#endif
#endif
