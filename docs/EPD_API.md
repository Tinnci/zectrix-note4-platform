# Zectrix EPD component API

Copy `components/zectrix_epd` into an ESP-IDF v5.5.2 project and add
`zectrix_epd` to the consuming component's `REQUIRES` list. The public API is
the C header `zectrix_epd.h`. C and C++ applications can call this API.

## Lifecycle

```c
zectrix_epd_config_t config;
zectrix_epd_get_default_config(&config);

zectrix_epd_handle_t epd = NULL;
ESP_ERROR_CHECK(zectrix_epd_new(&config, &epd));
ESP_ERROR_CHECK(zectrix_epd_power_on(epd));

// Perform one or more valid refresh operations here.

ESP_ERROR_CHECK(zectrix_epd_power_off(epd));
ESP_ERROR_CHECK(zectrix_epd_del(epd));
```

`zectrix_epd_new()` configures GPIO and SPI but leaves the external display
rail off. All refresh functions are synchronous: they return after the BUSY
handshake completes or a timeout expires. Normal BUSY phases use
`busy_timeout_ms` (2 seconds by default); each external grayscale refresh phase
has a 5-second timeout. BUSY polling yields at least one RTOS tick.

Driver operations wait at most 100 ms (at least one tick) for their mutex and
return `ESP_ERR_TIMEOUT` on contention; `zectrix_epd_is_powered()` returns
false if it cannot acquire the lock. This does not make multi-call
DisplayService transactions safe for concurrent owners. Finish all callers
before deleting the handle.

If a grayscale phase fails, the driver invalidates its controller state and
1bpp shadow. It sends no further power-off/OTP commands and does not reset a
possibly BUSY controller. Call `zectrix_epd_power_off()` to cut the external
rail, then power on and establish a full 1bpp frame before partial updates.
DisplayService performs this rail cleanup automatically outside an explicit
batch; callers must still end an open batch after failure. Successful gray
rendering retains its white preclear and OTP restoration sequence.

## Full 1bpp refresh

```c
uint8_t frame[ZECTRIX_EPD_1BPP_FRAME_BYTES];
memset(frame, 0xFF, sizeof(frame));  // White
ESP_ERROR_CHECK(zectrix_epd_refresh_full_1bpp(epd, frame, sizeof(frame)));
```

The image is 400 x 300, row-major and MSB first. `0` is black, `1` is white.
The buffer must be exactly 15,000 bytes.

## Partial 1bpp refresh

```c
zectrix_epd_rect_t rect = {.x = 80, .y = 96, .width = 64, .height = 32};
uint8_t patch[(64 / 8) * 32];
memset(patch, 0x00, sizeof(patch));
ESP_ERROR_CHECK(zectrix_epd_refresh_partial_1bpp(
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

Do a full refresh after eight actual partial refreshes to control ghosting.
`DisplayService` owns this policy for platform applications and can refresh
fully sooner based on the actual number of black/white transitions. The raw
driver performs the requested operation without applying that service policy.

## Dirty-region query

```c
zectrix_epd_rect_t source = {.x = 0, .y = 0, .width = 400, .height = 300};
zectrix_epd_rect_t dirty;
ESP_ERROR_CHECK(zectrix_epd_find_dirty_1bpp(
    epd, &source, frame, sizeof(frame), &dirty));
```

`zectrix_epd_find_dirty_1bpp()` accepts the same rectangle and packed pixels
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

`zectrix_epd_analyze_1bpp()` accepts the same input and returns a
`zectrix_epd_diff_t`: `dirty` contains the exact bounds and `changed_pixels`
counts black-to-white and white-to-black transitions. Unchanged pixels inside
the bounding box, byte-alignment neighbors and row padding do not contribute.
The count ranges from zero to 120,000. This query has the same locking,
allocation and error behavior as `zectrix_epd_find_dirty_1bpp()`.

The display service uses this count to select the existing full OTP refresh
when a submission changes at least 30,000 pixels, or the accumulated partial
transitions plus the pending submission reach 60,000 pixels. See
[M2_PLATFORM_CONTRACT.md](M2_PLATFORM_CONTRACT.md) for the complete policy.

## Full 4bpp refresh

```c
uint8_t gray[ZECTRIX_EPD_4BPP_FRAME_BYTES];
memset(gray, 0xFF, sizeof(gray));
ESP_ERROR_CHECK(zectrix_epd_refresh_full_4bpp(epd, gray, sizeof(gray)));
```

The image is 400 x 300 with two pixels per byte. The high nibble is the left
pixel. Values range from `0` black to `15` white. The buffer must be exactly
60,000 bytes.

To reduce ghosting, do a white full 1bpp refresh before 4bpp.
After 4bpp, establish another full 1bpp base before you use partial refresh.

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

`zectrix_epd_del()` requires all callers to have finished their transactions.
It powers off the panel, removes its SPI device, frees an owned bus and releases
DMA/shadow storage. Panel control pins and owned SPI signal pins have their
input/output buffers and pull resistors disabled, preventing signal-line drive
into the unpowered panel. A borrowed bus retains its MOSI/SCLK configuration.
