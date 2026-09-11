# Display physics observations and adaptive scheduling

R1.4 replaces the eight-partial-frame and accumulated-pixel scheduling limits
with a bounded spatial debt model. Small clock and focus changes can remain
partial longer; dense, concentrated or cold-screen transitions clean sooner.
The 30,000-pixel single-update cleanup rule, explicit Quality/FullClean,
unknown-image recovery, grayscale white preclear and rail cleanup remain.
Counts in `display status` are diagnostic totals, not refresh deadlines.

This is an analytical foundation with observable inputs and adjustable
coefficients. Its debt is a dimensionless risk score, not a measurement of
optical ghosting, charge or contrast. Default energy coefficients are absent;
the firmware does not invent a measured power or battery-life estimate.

## Ownership and cost

`DisplayService` remains the synchronous foreground owner. The driver adds
transition counts to the existing byte-wise shadow comparison, skipping equal
bytes. It splits a changed source byte at most once at an 80-pixel tile edge.
The partial write's second bounds scan omits this extra counting. No new
framebuffer, task, timer, sensor polling loop or per-frame allocation is added.

The driver accumulates successful SPI bytes and elapsed BUSY waits inside its
existing operations. Two bounded counter copies bracket each attempted
physical refresh using the existing driver mutex. The recorder itself has no
locks, atomics, callbacks, logging or allocation. Sixteen fixed records retain
the latest attempts; export copies at most four records at the existing CLI
owner safe point. The CLI task formats only that copy. This is single-owner
storage, not a ring that arbitrary tasks or ISRs may read concurrently.

The recorder is 1,800 bytes and each record is 112 bytes on both the Host and
the ESP32-S3 ELF. The model, including its 112-byte parameter dictionary,
occupies 280 bytes. The complete DisplayService is 2,296 bytes on ESP32-S3;
the additional driver counter structure is 48 bytes. These live in
the service allocated at initialization; they do not grow during reading.
There is a small bounded CPU/copy cost, not literally zero execution overhead.
Full and Minimal both use the same scheduling implementation.

## What a frame records

| Observation | Meaning and boundary |
| --- | --- |
| `started_us`, `duration_us` | Monotonic submission time and synchronous service duration, including analysis and owned power transitions; independent of RTC corrections. |
| `x,y,width,height` | Actual byte-aligned partial controller window, or 400 x 300 for a full refresh. |
| `black_to_white`, `white_to_black` | Exact 1bpp transitions relative to a trusted shadow. Unknown for initial/error recovery and grayscale. |
| `spi_bytes` | Successfully completed command, parameter, waveform, RAM and temperature-read bytes. A failed transaction's uncertain partial transfer is excluded. |
| `ram_bytes` | Successfully completed native 2bpp RAM payload. Full 1bpp is 30,000 bytes; gray includes the white preclear and every gray pass. |
| `busy_us` | Sum of observed BUSY wait durations, including reset, temperature and power phases, and timeouts. |
| `refresh_busy_us` | BUSY waits associated specifically with display-trigger phases. |
| `waveform_triggers` | Completed trigger commands, including the gray preclear. A trigger can subsequently time out. |
| Environment and age | Samples available **before** the decision, with monotonic ages. `UINT32_MAX` age means unavailable or a future timestamp. |
| `panel_temperature_centi_c` | A new controller temperature reading obtained during this frame, distinguished from the decision's cached temperature. |
| Projected/committed debt | The proposed partial update's mean/peak and the state after completion. A successful full clean commits zero; an error retains the previous debt with an invalid image baseline. |
| Mode, reason, error, revision | Selected refresh path, decision cause, ESP result and caller-supplied parameter revision. |

The existing driver temperature read runs during full OTP refresh/preclear.
Partial updates retain internal controller compensation without an extra SPI
read or bus reconfiguration. Normal controller bytes 0–85 are recorded as
0–85 °C. Negative/reserved byte encodings are unqualified observations and
remain unavailable; the existing OTP encoding is unchanged. A borrowed SPI
bus's fallback 25 °C is never reported as a sensor measurement. The analytical
model itself accepts supplied temperatures down to −40 °C for later qualified
sensors and Host simulation.

The shell feeds the PowerService's existing five-second battery sample.
Missing/absent batteries invalidate the value. This cadence can reveal voltage
trends, but cannot resolve a transient voltage sag during a waveform. Neither
CLI command starts an ADC conversion, powers the panel or changes a sample's
age. The default freshness limit is 60 seconds.

BUSY timing has the resolution and observation boundaries of the existing
10 ms yielding poll. In particular, the gray path's fixed delay before its
BUSY wait is part of total duration, not observed BUSY time. These fields are
not logic-analyzer measurements of the BUSY pin or current pulses. Explicit
batches exclude the outer BeginBatch/EndBatch operations; flag 16 identifies
that different power context. Gray still uses the original multi-pass LUTs.

## Spatial model

The 400 x 300 panel is divided into 5 x 4 equal 80 x 75 tiles, each with
`A = 6000` pixels. For each tile define:

- `f = (N_black_to_white + N_white_to_black) / A`, the flipped fraction;
- `b = (N_white_to_black - N_black_to_white) / A`, signed transition imbalance;
- `a = area(tile intersect driven_window) / A`, the driven fraction, including
  unchanged pixels inside the controller window;
- `q`, signed polarization memory; and `d`, nonnegative ghosting debt.

For idle interval `dt`, use the backward-Euler RC retention function
`R(z, tau, dt) = z * tau / (tau + dt)`. A zero time constant disables decay.
For temperature/supply multiplier `g`, one proposed partial update is:

```text
q_next = clamp(R(q, tau_memory, dt) + g * b)
d_next = clamp(R(d, tau_debt, dt)
               + g * (w_window * a
                    + w_flip * f
                    + w_concentration * f²
                    + w_memory * abs(q_next) * f))
```

The window term accounts for common drive exposure. The quadratic term makes
concentrated transitions more expensive than equally many distributed flips.
Signed memory captures directional history, while the nonnegative debt still
increases when repeated black/white reversals cancel that memory. Relaxation
remains positive for arbitrarily long gaps. The default debt decay is disabled:
idle time alone cannot establish that a visible ghost has disappeared.

These observable proxies are preferable here to fitting a full microcapsule
Markov or multi-RC network without optical/current measurements. The SSD2683's
partial RAM already encodes old/new transitions as `00`, `01`, `10`, `11`.
The model measures `01`/`10` and the remaining driven area; it does not infer
unobserved analog charge from those digital codes or claim to identify the
internal OTP pulse sequence.

Debt/memory use Q16.16, gains/weights Q8.8, with bounded 64-bit intermediates
and saturation at 64 debt units. Positive stress rounds upward to prevent
small coefficients from making one-pixel flips vanish through quantization.
There is no floating-point work in the firmware model. Defaults are:

| Parameter | Default |
| --- | --- |
| Window / flip / concentration / memory weights | 0.125 / 1 / 1 / 0.25 |
| Global mean / local peak cleanup budgets | 0.75 / 4 debt units |
| Signed-memory relaxation / debt relaxation | 120 seconds / disabled |
| Temperature gain at −10, 0, 10, 25, 40 °C | 2.5, 2, 1.5, 1, 1 |
| Missing/stale temperature gain | 1.5 |
| Fresh battery below 3500 mV | Multiply by 1.25 |

Temperature gain interpolates linearly between the knots and clamps at the
ends. It is an adjustable approximation to slower cold-particle response,
not a fitted Arrhenius activation energy. Supply compensation likewise remains
a conservative heuristic pending measurement.

For Auto/Fast, a valid unchanged submission returns without a refresh, record,
model decay or power operation. Otherwise select a full refresh for an unknown
baseline, at least 30,000 pending transitions, a projected local peak at its
budget, or a projected global mean at its budget. Explicit Quality/FullClean
and grayscale keep their existing paths. In a packed-patch call, cleanup uses
the supplied full fallback. The record retains the patch-based decision but
counts transitions of the full image actually submitted.

Prediction cannot mutate history. Only successful partial completion commits
it, after owned rail cleanup. A failed write, BUSY wait or rail shutdown leaves
old debt in place and invalidates partial use. The next full recovery resets
it. A successful gray operation also requires a new full 1bpp base before
partial use. Failure of a separate EndBatch invalidates that base as before.
Nothing schedules background cleanup or wakes a sleeping device.

## Calibration hook and energy estimates

`display.physics_parameters()` returns the current dictionary;
`display.SetPhysicsParameters(parameters)` installs a foreground-owned copy.
It rejects zero/overflowing budgets and zero flip/response gains,
without clearing existing debt. `revision` is an ordinary caller-supplied
number, included in every frame. There is no NVS write or separate calibration
file format. `display model` prints the active weights, budgets, relaxation,
temperature/supply response and all per-mode energy coefficients.

Each mode can optionally supply an independently calibrated linear energy fit:

```text
E_uj = fixed_uj + busy_power_uw * busy_us / 1,000,000
       + (spi_nj_per_byte * spi_bytes
          + black_to_white_nj * N_black_to_white
          + white_to_black_nj * N_white_to_black) / 1,000
```

This estimate is emitted only for successful frames with driver metrics and
an explicitly calibrated coefficient set. Direction-dependent coefficients
also require known transitions. Initial defaults emit no energy value.
Calibration must state the temperature, supply and batch regime covered by its
coefficients; the formula does not claim accuracy outside the measured regime.
Mode-specific constants can absorb unobserved reset/booster/LUT overhead,
while BUSY and SPI terms provide independent fitting inputs. Full/partial/gray
fits can differ without changing driver waveforms or application code.

For future measurements, collect synchronized optical/current traces and
terminal captures, group by mode, temperature, battery age and batch flag,
then fit nonnegative coefficients against measured energy and optical error.
Use separate reading sessions to assess the fit. Update the dictionary and
rerun the same scenarios before applying the measured parameters to a panel.

## Export and simulation

On the maintenance terminal:

```text
display status
display model
display telemetry
display telemetry 4
```

`display telemetry [after-sequence]` returns at most four records **after** the
exclusive cursor. Start with zero and use the returned `next` until it reaches
`latest`. A `lost` count identifies overwritten records. Records are numbered
from one per boot; a future cursor restarts at the oldest retained frame.
Use a new capture after reboot, or explicitly restart the cursor at zero.
Each result is an immutable copy even if new frames overwrite the ring while
the CLI transmits it. Ctrl+C/cancellation retain the existing owner protocol.

Every frame produces three CSV rows so no terminal chunk exceeds 256 bytes:

```text
frame,sequence,started_us,kind,reason,error,flags,x,y,width,height,black_to_white,white_to_black,duration_us,busy_us,refresh_busy_us,spi_bytes,ram_bytes,waveform_triggers
env,sequence,temperature_centi_c,temperature_age_ms,battery_mv,battery_age_ms,panel_temperature_centi_c,gain_q8
debt,sequence,model_revision,projected_mean_q16,projected_peak_q16,committed_mean_q16,committed_peak_q16,energy_uj
```

Modes are `0=none, 1=full-1bpp, 2=partial-1bpp, 3=full-4bpp`.
Reasons are `0=partial, 1=recovery, 2=quality, 3=explicit-clean,
4=high-contrast, 5=global-debt, 6=local-debt, 7=gray, 8=driver-error`.
Flag bits are `1=known transitions, 2=known driver metrics,
4=new panel temperature, 8=calibrated energy estimate, 16=power batch`.

Convert a terminal capture to one CSV row per frame:

```sh
uv run --no-project tools/display-telemetry.py capture.txt -o frames.csv
bash tools/test-display-state.sh simulation.csv
```

The converter preserves ages and real zero values, leaves unknown measurements
empty, reports overwritten records and rejects incomplete/mixed frame rows.
Repeated page captures retain repeated rows; consumers can deduplicate by
sequence within a single boot. The simulation CSV contains every decision and
committed debt for 24,576 deterministic updates. Both CSV files are directly
readable with Python's `csv` module or `pandas.read_csv` in a notebook.

Each simulation below has 4,096 updates. Initial full-base establishment is
outside the totals. The first five use a full-screen driven window to compare
model terms; the last uses the stated small window. Samples are supplied
directly; this does not simulate
real sensor refresh cadence or claim panel quality/energy savings.

| Scenario | Partial | Full |
| --- | ---: | ---: |
| Sparse reading, 6,000 flips across tiles, 25 °C, 20 s/page | 3,277 | 819 |
| Dense reading, 20,000 flips across tiles, 25 °C, 20 s/page | 2,731 | 1,365 |
| Sparse reading, 0 °C, 20 s/page | 2,731 | 1,365 |
| 6,000 flips concentrated in one tile, 100 ms/update | 2,048 | 2,048 |
| 6,000 flips distributed across tiles, 100 ms/update | 3,277 | 819 |
| Status strip, 100 flips in a 40 x 10 window, 60 s/update | 4,071 | 25 |

## Reference comparison

The source review on 2026-09-12 revisited CrossPoint's
[EpubReaderActivity](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/reader/EpubReaderActivity.cpp),
[ActivityManager](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/ActivityManager.cpp),
[SleepActivity](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/boot_sleep/SleepActivity.cpp)
and [CrossPointWebServer](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/network/CrossPointWebServer.cpp),
and Flipper Zero's
[SceneManager](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/scene_manager.c)
and [ViewPort](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/view_port.c).
Links follow upstream branches.

CrossPoint provides cooperative section pagination, coalesced activity updates,
page refresh cycles, explicit grayscale image handling, final sleep composition
and streamed upload cleanup. Note4 preserves its existing streamed Reader,
committed bookmarks, static sleep cover and bounded Wi-Fi transfer ownership.
R1.4 moves cleanup choice beneath those applications into DisplayService so a
clock tick, a focused tile and a book page share one physics policy.

Flipper's enter/event/exit handlers and viewport invalidation support clear
scene lifetimes. Note4 retains its deferred SceneManager and one foreground
canvas owner. It does not invoke UI callbacks from BUSY waits or add a render
task to publish telemetry. Reader presentation acknowledgements still wait for
the successful physical result. See [DISPLAY_RESPONSIVENESS.md](DISPLAY_RESPONSIVENESS.md)
and [FIRMWARE_UI_STUDY.md](FIRMWARE_UI_STUDY.md) for the surrounding architecture.

## Verification

Host coverage includes independent pixel-by-pixel checks across every X
alignment and tile boundaries, the documented equations, temperature and age
fallback, parameter changes, long reading/high-frequency simulations, numerical
saturation, ring overwrite, CLI cancellation and CSV reconstruction. The real
SSD2683/service fixture checks traffic counts, cold/invalid controller samples,
partial and power failures, BUSY timeout duration, gray pass accounting and
full recovery. The existing UI scenarios still transfer 44 RAM bytes for a
status-only minute change and coalesce nine queued Home inputs into one draw.

On 2026-09-12, all 39 Host targets, focused display/model ASan/UBSan runs,
ShellCheck and Full/Minimal ESP32-S3 builds passed. Profile comparison retained
module exclusion and boot protection. Final application sizes are:

| Metric | Full | Minimal |
| --- | ---: | ---: |
| Firmware bytes | 2,492,240 | 520,096 |
| Increase from R1.3 | 5,984 | 3,568 |
| Static internal RAM bytes (unchanged) | 201,815 | 108,315 |

Full retains 653,488 bytes in each 3 MiB slot. Minimal is
79.1% smaller. No partitions, font assets or stored book formats changed.
The recorder and model are initialized within the service allocation and do
not contribute new static BSS.

The connected ESP32-S3 Full flash/boot smoke passed, including the first
Launcher frame, 15 applications, boot confirmation and USB CLI. A subsequent
real CLI capture and CSV conversion observed a full 1bpp startup frame with
30,000 RAM bytes, 30,023 total SPI bytes, 1,286,990 us service duration,
1,154,059 us observed BUSY waits and 859,945 us display-phase BUSY waits.
The controller reported 27 °C and the cached battery sample was 4,120 mV,
53 ms old at submission. Energy stayed explicitly uncalibrated. These are one
frame's device observations, not optical validation or current measurement.
The captured application-task stack watermark was 6,028 free bytes.
Physical ghosting, negative sensor encodings, instantaneous voltage sag and
energy coefficients still require instrument qualification.
