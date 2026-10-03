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
30,000 for mono plus a rotated patch, and 60,000 only when rotated grayscale is
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
