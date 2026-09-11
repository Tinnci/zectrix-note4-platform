# Status bar micro-icons

E1.10 uses original monochrome masks for Bluetooth, Wi-Fi and power states in
the existing 24px status viewport. The clock stays on the left; fixed radio,
power and percentage slots align on the right. The same pixels serve English,
Chinese and Minimal builds. A language change redraws application copy while
the status graphics retain their appearance.

## Battery and power

The battery occupies 20 x 10 pixels, including its 2 x 4 terminal. Cut corners
and a one-pixel inner gap separate the outline from five 2 x 6 charge cells.
Zero percent is empty; 1–20%, 21–40%, 41–60%, 61–80% and 81–100% light one
through five cells. The right-aligned percentage retains the sampled value,
clamped to 100. Discharging at 10% or less adds a warning mark.

| Power observation | Companion mark or battery content |
| --- | --- |
| Charging | Lightning bolt beside the battery |
| Charger reports full | Check mark; the measured percentage is retained |
| External power, without charging/full | Plug |
| Charger fault | Exclamation mark |
| ADC sample unavailable | Question mark inside the battery and `--%` |
| Battery absent | Cross inside the battery and `--%`; external power can still show the plug |

The companion mark's priority is fault, present-battery full, present-battery
charging, external power, then low battery. Absence suppresses charge cells,
charging and full even if another sampled flag is still set. A fault preserves
the charge cells and percentage. A 100% ADC estimate alone does not set the
charger-full mark. These observations come from the existing five-second
PowerService sample; rendering performs no ADC or charger access.

## Radio vocabulary

A 7 x 13 Bluetooth rune and 13 x 9 Wi-Fi fan each have a 5 x 7 companion mark.
Their optical centers align with the battery and 16px text. The fan identifies
Wi-Fi; its arcs do not encode RSSI.

| Mark | Meaning and source |
| --- | --- |
| Slash | Off; saved credentials alone leave Wi-Fi off |
| Hollow circle | Ready or connecting: BLE advertising/pairing/securing, Wi-Fi startup/association/stopping, or a listening hotspot |
| Solid dot | BLE transport connected, or station networking after IP acquisition |
| Opposed arrows | A BLE frame in flight or waiting for consumption; a Wi-Fi resource transfer or active book upload |
| Exclamation mark | BLE fault or failed Wi-Fi shutdown |

`BleLink` copies its existing transmit/receive work state under the existing
lock. `ConnectivitySnapshot` carries `ble_data_active` and `wifi_data_active`;
the book-sharing path uses `client_active` to distinguish an upload from an
open server. A waiting hotspot shows the hollow circle. A waiting station
server shows the connected dot. Fault and stopped states take precedence
over activity. A connected BLE icon describes transport, without claiming
peer authorization or synchronization convergence.

Activity is sampled on the existing foreground loop. A short exchange that
finishes between samples may never show arrows. The icons have no animation,
packet timer, retained activity timeout or additional radio polling. Only
visible state changes request a draw.

## Rendering and ownership

`DrawStatusBar(canvas, state, inverted)` paints the current clip on the shared
15,000-byte canvas. The optional polarity defaults to black on white; inverse
output complements the entire status strip. Masks, battery geometry, text and
separator all use that polarity. Drawing stays within rows 0–23 and preserves
the caller's clip. Charge marks and all percentage widths leave adjacent
slots stationary.

The masks occupy 198 bytes in Flash. Integer row tests and five bounded fill
operations use no drawing heap allocation, floating-point work, icon font or
additional framebuffer. Minimal retains its existing font/module exclusions.
State equality compares visible percentage and marks, so changes to an invalid
measurement, a clamped percentage or a lower-priority power flag do not
invalidate the viewport.

The foreground owner still combines pending status and content in one commit.
DisplayService chooses the dirty rectangle, waveform and physics-based cleanup.
Gray previews retain their owned gray buffer and established preclear path.
The final sleep cover keeps its static snapshot header. The SceneManager and
ViewPort lifecycle remains as described in [M3](M3_APPLICATION_CONTRACT.md).

CrossPoint's [BaseTheme battery rendering][crosspoint-battery] is a reference
for compact outlines, fill and charging recognition. Note4 uses separate
charger flags and a companion mark to keep the measured charge fill readable.
Flipper's [SceneManager][flipper-scenes] and [ViewPort][flipper-viewport] inform
the existing scene ownership and visible-change invalidation. The established
[streaming reader, static covers and local transfer](FIRMWARE_UI_STUDY.md)
continue through those same boundaries. No upstream code or icon assets were
imported.

## Previews and verification

```bash
mkdir -p build-status-icons/previews
ZECTRIX_UI_PREVIEW_DIR="$PWD/build-status-icons/previews" bash tools/test-display-service.sh
bash tools/test-host.sh
ZECTRIX_DISPLAY_SANITIZE=1 bash tools/test-display-service.sh
ZECTRIX_LOCALIZATION_SANITIZE=1 bash tools/test-localization.sh
source tools/activate-dev-env.sh
bash tools/build-firmware.sh --profile full
bash tools/build-firmware.sh --profile minimal
```

The display fixture writes `status-icons.pbm` with seventeen normal/inverted
strip pairs and `status-icons.txt` with ASCII pixel rows. Chinese runs write
the same names with `zh-` prefixes. Cases include 0%, 5%, 20%, 50%, 80%, 100%,
charging, charger-full at 98%, external power, faults, unknown/absent batteries
and all five radio states. Existing Home, Reader, transfer and settings previews
show their actual composed status bar. These files are generated inspection
artifacts, with no stored image comparison. To convert the contact sheet:

```bash
uv run --no-project --with pillow python -c 'from PIL import Image; Image.open("build-status-icons/previews/status-icons.pbm").save("build-status-icons/previews/status-icons.png")'
```

Host checks cover distinct radio states, monotonic charge fill, mark precedence,
hidden-state equality, clipped drawing, both polarities, three font compositions
and allocation-free rendering. Integration exercises the production snapshot
adapter, idle hotspot versus active upload, language-independent status, failed
display recovery and gray-image preservation. SSD2683 simulation measures
44 native RAM bytes for the tested minute change and 14 bytes for a Wi-Fi
ready-to-active mark change; application pixels are preserved.

| ESP-IDF 5.5.2 artifact | Full | Minimal |
| --- | ---: | ---: |
| Application binary | 2,492,576 bytes | 520,304 bytes |
| Change from R1.4 | +336 bytes | +208 bytes |
| Static internal RAM | 201,815 bytes (unchanged) | 108,323 bytes (+8) |

The status renderer object grows by 252 bytes including its masks and literals.
All 39 Host targets, display/localization ASan/UBSan, Full/Minimal builds and
the existing profile comparison passed. Pixel inspection covered the normal
and inverse contact sheet and composed pages. The connected ESP32-S3 Full
flash/boot smoke passed, including the first Launcher frame and boot confirmation.
These checks do not measure physical contrast or radio throughput.

[crosspoint-battery]: https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/components/themes/BaseTheme.cpp
[flipper-scenes]: https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/scene_manager.c
[flipper-viewport]: https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/view_port.c
