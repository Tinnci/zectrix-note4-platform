# Display ownership and frame transforms

The display path has three concrete layers:

1. UI (`Canvas`, `PageShell`, `UiEngine`) renders logical page geometry and text.
   It owns no SPI, GPIO, panel rail or waveform selection.
2. `DisplayService` owns task-local sequencing, power batches, orientation,
   unchanged-frame suppression, partial/full policy, physics and telemetry.
3. The SSD2683 driver owns bus transactions, BUSY waits, waveform execution,
   physical byte-aligned windows and the confirmed 1bpp shadow.

`private_include/frame_transform.h` contains allocation-free pixel transforms.
It knows packed pixel geometry, not controller registers or refresh policy.
Half-turn mono/gray transforms, tightly packed portrait rows and row-padded
partial patches are distinct operations. They must not share stride assumptions:
a 300-pixel portrait scanline starts halfway through a byte on alternating rows.

The service keeps a typed opaque driver handle without importing GPIO/SPI into
its public header. Applications continue to use the service, never that handle.
No speculative virtual backend or additional hardware task is introduced.

Rotation scratch grows by bounded tiers: 15,000 bytes for mono orientation,
30,000 for mono plus a rotated patch or 2bpp, and 60,000 only when rotated 4bpp is
requested. A failed growth preserves the previous buffer and orientation and
does not power the panel. Capacity is retained until service destruction to
avoid repeated allocation during navigation. The driver shadow remains separate.

Lock-screen portrait rotation is explicit and does not temporarily mutate the
application's orientation preference. Returning from a successful lock-screen
frame still requests a clean first application frame. Driver errors continue to
invalidate the service model; successful hardware completion alone commits debt
and shadow state. Gray-to-mono transitions retain their existing clean-refresh
rule.

Host coverage checks every pixel in both portrait rotations, half-turn round
trips, unaligned patch padding, allocation failures, orientation changes and
real driver transactions under the existing fault-injected SPI/BUSY simulator.
These checks do not establish physical panel quality or energy consumption;
those still need Note4 measurements.

## Waveforms and calibration

The driver separates transport/recovery (`note4_epd.cc`), the fixed vendor
waveform family (`private_include/ssd2683_waveform.h`), and a GPIO-free typed
profile/codec (`note4_epd_calibration.cc`). The maintained offline waveform catalog lives
in `tools/display-calibration/ssd2683_waveform_catalog.h`, not in the firmware include path.
Tests compare all five default waveform payloads and all 1,280 packed code
entries against the previous implementation. Whole eleven-byte records are
copied: timing bytes must travel with drive symbols.

The five-pass order, code-0 hold, white preclear, analog settings, BUSY recovery
and gray-to-mono cleanup are unchanged. A 1,280-byte flash lookup replaces
per-pixel level searches; a single 535-byte waveform is constructed on the
stack per pass. Refreshing allocates no calibration buffers. This reduces CPU
encoding work, not physical BUSY duration or panel power consumption by itself.

The composition root owns `note4_display_calibration_store.cc`; the driver and
display service have no NVS dependency. One versioned 92-byte blob holds gray
recipes, optical estimates and measurement flags. NVS supplies integrity and
single-key recovery; no extra digest, raw-register upload or multi-key save is
introduced. Saving does not change the active profile. Startup restores before
the first frame; missing data selects builtin defaults, invalid/incompatible
data reports a warning and uses defaults without erasing the saved record.
USB local confirmation is required for update/reset. Factory reset removes
calibration with the other settings; user-file wipe does not.

The default optical estimates are historical, with no measured flags set for
the current sequence/panel. Editing a recipe clears all measured flags because
later hold phases affect previously painted levels; an explicitly supplied
measurement may mark the edited level. Structural validation does not establish
optical monotonicity.

## Frame input and model settings

Native 2bpp uses 30,000 bytes instead of 4bpp's 60,000, supports all four
orientations, and never expands to 4bpp. Targets 0,333,667,1000 permille are
quantized through the active profile directly into native controller RAM.
White base plus five passes is unchanged: less host memory does not imply fewer
triggers or faster physical refresh. The UI no longer adds a redundant white
refresh before the driver's preclear. Any phase failure invalidates the baseline
and stops controller writes.

Ghosting/environment/energy coefficients have a separate typed 112-byte
`epd.model.v1` record, restored before the first frame. Missing data uses defaults;
invalid data is preserved with a warning and fallback. `display model` shows
active and next-boot dictionaries. USB-confirmed `model-set`/`model-reset` change
only this model, not optical calibration. Saved estimates are not physical
measurement evidence. See [CLI](MAINTENANCE_CLI_CONTRACT.md).

Transport, calibration, policy and telemetry use independent
[component logging](LOGGING.md), not a dependency on maintenance CLI.
