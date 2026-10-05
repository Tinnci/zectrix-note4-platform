# Driver, logging and runtime integration

Local software verification: **2026-10-05**, macOS 15.7.9 x86_64.
This record covers the working-tree implementation, not a published release
or device qualification. The Note4 was not connected; no firmware was flashed.

## Maintained implementation

- SSD2683 transport/recovery, waveform construction, calibration codec and
  display policy are separate. Native 2bpp supports four orientations without
  a 4bpp expansion buffer. Default gray waveforms retain white preclear plus
  five passes; optical quality and BUSY duration are not inferred from RAM savings.
- Optical calibration (`epd.cal.v1`, 92 bytes) and display model coefficients
  (`epd.model.v1`, 112 bytes) save independently through platform-owned NVS.
  Updates are USB-confirmed and take effect at the next boot. Invalid saved
  records remain available for diagnosis rather than being silently erased.
- `note4_log` owns bounded RAM capture and structured events independently of
  CLI. Project DEBUG is compiled in but defaults to INFO. Producer verbosity
  and observer filtering are distinct; SDK-wide radio DEBUG is not enabled.
- WAMR 2.4.5 and Wasm3 0.5.0 have maintained optional C execution backends.
  Lua remains the application runtime and package format. One engine is selected
  when enabled; neither Wasm backend is shipped in the default Full profile.
- SDK 6.0.3 is a separate build-compatible lane, not the default device/release
  SDK. SDK 5.5.2 remains the default. Build directories, Python environments
  and SDK 6 component locks are isolated; the root dependency lock is unchanged.

## Executed checks

| Check | Result |
| --- | --- |
| Complete host suite | 48 targets passed, plus architecture checks; two workers |
| SSD2683/display simulation | Native 2bpp/4bpp equivalence in four orientations; phase failure recovery, packing and allocation failures passed |
| Display and storage ASan/UBSan | Passed |
| Wasm admission C11 ASan/UBSan | Passed, including 10,000 malformed-input cases |
| Lua, WAMR and Wasm3 ASan/UBSan | All passed; bounded loops, initialization handling, quotas, 32/64-bit constants and repeated cleanup |
| Firmware package tests | Nine passed, including SDK 6 esptool command spelling |
| Offline waveform catalog | 58 entries listed; exported vendor payload contained all 535 bytes |
| ShellCheck and whitespace checks | Passed |
| GitHub workflow | Parsed locally; added jobs have not been run on GitHub for this working tree |

The full host run took 365.07 seconds while firmware builds were running.
This is a correctness result, not a controlled performance comparison.
Machine-readable results and individual logs remain in ignored build outputs:

- `build-host/host-tests/run-awv688pg/results.json`
- `build-runtime-qualification-asan/sanitized-results.json`
- `build-runtime-qualification/results.json`
- `build-runtime-qualification-idf6/results.json`

### Fixture memory, not application/device memory

| Engine | Normal fixture tracked peak | Live allocations after close |
| --- | ---: | ---: |
| WAMR | 77,238 bytes | 0 |
| Wasm3 | 78,785 bytes | 0 |
| Lua qualification adapter | 9,061 bytes | 0 |

Each completed 100 load/call/unload cycles. Wasm accounting includes allocation
headers and owned module bytes; the Lua qualification allocator counts payload
only. These figures are not an equivalent-memory ranking or ESP32-S3 heap/stack
measurements. Guest loops exhausted their instruction budgets; rejected Wasm
start sections/automatic constructors did not enter unbounded initialization.

## ESP32-S3 build results

Smallest existing application slot: **3,145,728 bytes**. Sizes below are binary
image sizes, not ELF sizes. Full and Minimal builds passed on both SDKs.

| SDK | Image | Bytes |
| --- | --- | ---: |
| 5.5.2 | Full, Wasm off | 2,718,656 |
| 5.5.2 | Minimal | 703,840 |
| 6.0.3 | Full, Wasm off | 2,784,256 |
| 6.0.3 | Minimal | 681,184 |
| 5.5.2 | Full + WAMR qualification fixture | 2,785,584 |
| 5.5.2 | Full + Wasm3 qualification fixture | 2,785,136 |
| 6.0.3 | Full + WAMR qualification fixture | 2,850,000 |
| 6.0.3 | Full + Wasm3 qualification fixture | 2,850,096 |

Runtime qualification images retain calls into the same production adapter,
not a duplicate VM implementation. They are link/size evidence only: hostile
fixtures are not executed on a physical device. All eight images fit the existing
application slot; the largest qualification image leaves 295,632 bytes free.

SDK 6 exposed a NimBLE adjacent-entry capacity warning in the one-bond
configuration. A build-owned capacity-bounded source copy preserves that
security limit and compiler warnings. WAMR's ESP-IDF port also needed an
explicit `sys/stat.h` include; unused WASI file/socket adapters are excluded
from the firmware build. Wasm3's code-page constant stores and constant-table
access use `memcpy` to fix
GCC 15 strict-aliasing errors without suppressing warnings. Integer and floating
point constant regression checks passed. SDK and upstream release checkouts
are not patched.

## Remaining physical evidence

1. Measure gray reflectance, ordering and ghosting across panel temperature and
   battery conditions. Defaults contain estimates, not newly measured values.
   The offline catalog retains candidates; it does not auto-activate them.
2. Measure refresh energy and BUSY times before tuning persisted physics
   coefficients. Lower frame storage does not increase physical resolution or
   establish a shorter refresh sequence.
3. Measure S3 native stack, worst-case parse/call latency and recovery under
   constrained PSRAM before accepting arbitrary third-party Wasm applications.
   Native host callbacks must remain bounded and nonblocking.
4. Qualify SDK 6 display, RTC wake/sleep, radio coexistence and current draw
   before switching the default device/release SDK.

See [display architecture](../DISPLAY_ARCHITECTURE.md),
[logging](../LOGGING.md), [runtime limits](../../components/note4_runtime/README.md)
and [toolchain policy](../TOOLCHAIN_POLICY.md) for ownership and operating rules.
