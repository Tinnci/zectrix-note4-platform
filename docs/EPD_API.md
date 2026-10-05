# Note4 EPD component API

Copy `components/note4_epd` and its `note4_log` dependency into an ESP-IDF 5.5.2
or 6.0.3 project and add
`note4_epd` to the consuming component's `REQUIRES` list. The public API is
the C header `note4_epd.h`. C and C++ applications can call this API.

## Lifecycle

```c
note4_epd_config_t config;
note4_epd_get_default_config(&config);

note4_epd_handle_t epd = NULL;
ESP_ERROR_CHECK(note4_epd_new(&config, &epd));
ESP_ERROR_CHECK(note4_epd_power_on(epd));

// Perform one or more valid refresh operations here.

ESP_ERROR_CHECK(note4_epd_power_off(epd));
ESP_ERROR_CHECK(note4_epd_del(epd));
```

`note4_epd_new()` configures GPIO and SPI but leaves the external display
rail off. All refresh functions are synchronous: they return after the BUSY
handshake completes or a timeout expires. Normal BUSY phases use
`busy_timeout_ms` (2 seconds by default); each external grayscale refresh phase
has a 5-second timeout. BUSY polling yields at least one RTOS tick.

Driver operations wait at most 100 ms (at least one tick) for their mutex and
return `ESP_ERR_TIMEOUT` on contention; `note4_epd_is_powered()` returns
false if it cannot acquire the lock. This does not make multi-call
DisplayService transactions safe for concurrent owners. Finish all callers
before deleting the handle.

If a grayscale phase fails, the driver invalidates its controller state and
1bpp shadow. It sends no further power-off/OTP commands and does not reset a
possibly BUSY controller. Call `note4_epd_power_off()` to cut the external
rail, then power on and establish a full 1bpp frame before partial updates.
DisplayService performs this rail cleanup automatically outside an explicit
batch; callers must still end an open batch after failure. Successful gray
rendering retains its white preclear and OTP restoration sequence.

## Full 1bpp refresh

```c
uint8_t frame[NOTE4_EPD_1BPP_FRAME_BYTES];
memset(frame, 0xFF, sizeof(frame));  // White
ESP_ERROR_CHECK(note4_epd_refresh_full_1bpp(epd, frame, sizeof(frame)));
```

The image is 400 x 300, row-major and MSB first. `0` is black, `1` is white.
The buffer must be exactly 15,000 bytes.

## Partial 1bpp refresh

```c
note4_epd_rect_t rect = {.x = 80, .y = 96, .width = 64, .height = 32};
uint8_t patch[(64 / 8) * 32];
memset(patch, 0x00, sizeof(patch));
ESP_ERROR_CHECK(note4_epd_refresh_partial_1bpp(
    epd, &rect, patch, sizeof(patch)));
```

A successful full 1bpp refresh must establish the base image before a
partial update. The patch is tightly packed by row. Keep coordinates inside
the 400 x 300 panel and supply `ceil(width / 8) * height` bytes. Patch origins
and widths need not be byte-aligned; unused low bits in each row are ignored.

The driver compares the patch with its existing 1bpp shadow and sends the
smallest changed bounding box. It expands only the X bounds to multiples of
eight pixels for the SSD2683 window. Neighboring pixels retain their previous
values. An unchanged patch returns `ESP_OK` without a controller transaction;
the raw refresh API still requires the panel to be powered and ready.
The shadow changes only after a successful refresh.

`DisplayService` owns the spatial ghosting-debt policy for platform applications;
see [DISPLAY_PHYSICS.md](DISPLAY_PHYSICS.md). The raw driver performs the requested
operation without applying that policy. A standalone consumer must provide its
own cleanup schedule.

## Dirty-region query

```c
note4_epd_rect_t source = {.x = 0, .y = 0, .width = 400, .height = 300};
note4_epd_rect_t dirty;
ESP_ERROR_CHECK(note4_epd_find_dirty_1bpp(
    epd, &source, frame, sizeof(frame), &dirty));
```

`note4_epd_find_dirty_1bpp()` accepts the same rectangle and packed pixels
as partial refresh, including a complete 15,000-byte frame. It returns exact
pixel bounds before controller alignment, or `{0, 0, 0, 0}` for an unchanged
image. The query reuses the existing shadow under the driver mutex. It does
not allocate memory, modify the shadow, or access GPIO/SPI, and works while
the panel is powered off.

A valid 1bpp shadow is required. An invalid shadow returns
`ESP_ERR_INVALID_STATE`; all errors clear the output rectangle when supplied.
When submitting the refresh, keep the original source rectangle and packed
buffer together. The returned dirty rectangle does not change the source
stride or origin. The refresh operation recomputes the bounds under its lock.

`note4_epd_analyze_1bpp()` accepts the same input and returns a
`note4_epd_diff_t`: `dirty` contains the exact bounds and `changed_pixels`
counts black-to-white and white-to-black transitions. Unchanged pixels inside
the bounding box, byte-alignment neighbors and row padding do not contribute.
The count ranges from zero to 120,000. R1.4 also returns directional transition
counts for twenty 80 x 75 tiles, excluding source padding and unchanged bits.
This query has the same locking, allocation and error behavior as
`note4_epd_find_dirty_1bpp()`.

The display service retains the 30,000-pixel single-update full refresh and
uses the tiled observations to predict accumulated debt instead of counting
pages. See [M2_PLATFORM_CONTRACT.md](M2_PLATFORM_CONTRACT.md) for the policy.

`note4_epd_read_metrics()` copies cumulative successful SPI bytes, native RAM
bytes, observed BUSY time, display-phase BUSY time, trigger count and the last
qualified controller temperature sample. It uses the existing driver mutex and
performs no GPIO/SPI operation. Counters include failures up to the last completed
transfer/wait. Subtract consecutive snapshots to observe an operation; see
[DISPLAY_PHYSICS.md](DISPLAY_PHYSICS.md) for timing resolution, temperature validity
and explicit power-batch boundaries.

## Full 4bpp refresh

```c
uint8_t gray[NOTE4_EPD_4BPP_FRAME_BYTES];
memset(gray, 0xFF, sizeof(gray));
ESP_ERROR_CHECK(note4_epd_refresh_full_4bpp(epd, gray, sizeof(gray)));
```

The image is 400 x 300 with two pixels per byte. The high nibble is the left
pixel. Values range from `0` black to `15` white. The buffer must be exactly
60,000 bytes.

The driver establishes an OTP white base before the five gray passes; standalone
callers and the UI do not add another white 1bpp refresh. Gray codes are optical
targets, not alpha coverage.
After 4bpp, establish another full 1bpp base before you use partial refresh.

## Full 2bpp refresh

`note4_epd_refresh_full_2bpp()` accepts exactly 30,000 bytes, 400×300 row-major,
four MSB-first pixels per byte. Values 0..3 map to optical targets 0,333,667,1000
permille through the active profile, directly to controller RAM without a
60,000-byte expansion. It uses the same six-trigger sequence and failure cleanup
as 4bpp; it does not create extra spatial resolution, partial grayscale or
measured energy savings. `DisplayService::Present2Bpp(Quality,...)` handles all
four orientations.

## Calibration

`note4_epd_calibration.h` has no GPIO/SPI/ESP-IDF dependency. A profile contains
version/panel family, revision, per-level vendor-record selectors, normalized
reflectance in 0..1000, and measured flags. `note4_epd_calibration_default()`
supplies the existing gray sequence with historical optical estimates. No
default level is certified as measured on the current panel/sequence.

`note4_epd_calibration_validate()` rejects unsupported versions/panels, zero
revision, out-of-range selectors, non-increasing reflectance, equivalent driven
recipes, modified black/white endpoints and terminator edits. Only complete
vendor records can be selected; arbitrary timing/voltage bytes and phase
reordering cannot be uploaded. Valid recipes still require optical validation.

`note4_epd_calibration_encode/decode()` use exactly 92 little-endian bytes,
independent of compiler padding. Failed decoding leaves its output untouched.
`note4_epd_calibration_update()` validates a complete prospective profile,
increments revision, and clears optical evidence after a recipe change.
`note4_epd_calibration_quantize()` selects the nearest reflectance, ties toward
white; validate the profile once before processing pixels. It does not perform
automatic font gamma correction or change already encoded 4bpp level numbers.

```c
note4_epd_calibration_t calibration;
note4_epd_calibration_default(&calibration);
// Load and validate your saved profile here, with the panel rail off.
ESP_ERROR_CHECK(note4_epd_set_calibration(epd, &calibration));
```

`note4_epd_read_calibration()` copies the active profile without panel I/O.
`note4_epd_set_calibration()` copies a validated profile under the driver mutex,
rejects a powered panel, and invalidates the previous 1bpp baseline. Platform
consumers use `DisplayService::ReadCalibration/SetCalibration` instead; setters
also reject an active power batch.

The Note4 platform restores `epd.cal.v1` from the existing settings namespace
before its first frame. Missing records use defaults; damaged, incompatible or
unreadable records use defaults and retain the original data. Saving a profile
does not erase books, connectivity settings or firmware, and needs no firmware
rebuild. A full factory reset erases it; user-file wipe retains it.

USB commands:

```text
display calibration
display calibration-set 6 3 4 4 400 1
confirm <token>
display calibration-reset
confirm <token>
```

The example sets level 6's existing recipe to normalized reflectance 400 and
marks it measured; use a value actually measured on your panel, not this example
as a calibration recommendation. Arguments are decimal: level, base table,
alternate table, record mask, reflectance, measured (0/1). The black and white
endpoint recipes are fixed. Commands report active and saved values separately;
saved changes apply on the next reboot, never in the middle of a refresh.
Read failures block editing rather than overwriting retained data. Save errors
are reported and leave active rendering unchanged; inspect saved state before
retrying because a failed NVS commit is not proof that nothing reached flash.

## Error handling

The API returns standard `esp_err_t` values. Common errors include
`ESP_ERR_INVALID_ARG`, `ESP_ERR_INVALID_SIZE`, `ESP_ERR_INVALID_STATE` and
`ESP_ERR_TIMEOUT`. In a long-running product UI, do not use `ESP_ERROR_CHECK`
for a recoverable screen error. Log the return value and handle the error
instead.

## SPI ownership

The default configuration initializes the SPI bus. Set
`initialize_spi_bus = false` only when the application owns the selected SPI
host. The application must also configure compatible pins and DMA settings.

`note4_epd_del()` requires all callers to have finished their transactions.
It powers off the panel, removes its SPI device, frees an owned bus and releases
DMA/shadow storage. Panel control pins and owned SPI signal pins have their
input/output buffers and pull resistors disabled, preventing signal-line drive
into the unpowered panel. A borrowed bus retains its MOSI/SCLK configuration.
