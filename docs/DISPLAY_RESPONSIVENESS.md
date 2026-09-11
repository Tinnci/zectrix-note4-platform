# Display responsiveness and input concurrency

E1.3 reduces obsolete foreground draws after a slow refresh while retaining
one application/display owner. It applies the coalesced-update principle from
CrossPoint's [ActivityManager](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/ActivityManager.cpp)
and the callback/lifetime discipline of Flipper's
[SceneManager](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/scene_manager.c)
and [ViewPort](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/services/gui/view_port.c).
The existing streaming reader, local book transfer and static sleep covers
keep their ownership and persistence rules; see [FIRMWARE_UI_STUDY.md](FIRMWARE_UI_STUDY.md).

## Foreground scheduling

The board still samples buttons every 20 ms with 40 ms debounce, independently
of the foreground's SPI/BUSY waits. After a refresh, the shell dispatches at
most 16 pending events through SDK 1.1 `DispatchInput`. Dirty requests union
and Quality wins. The next render composes the latest application state and
pending status changes into the existing canvas. No extra framebuffer,
render task or background application is allocated.

Confirmation, any long press, a dispatch error or a foreground-generation
change ends the burst. The shell flushes the new foreground before consuming
later input. Shutdown exits without rendering the outgoing application.
Direction-only bursts finish with one idle callback, so streamed pagination
and timers receive work even with continuous input. An empty queue adds no
debounce or batching delay. Reader parsing retains its existing bounded
work/cancellation behavior; keys received while its engine is busy keep the
Reader controller's existing semantics.

Callbacks never run inside driver waits. `InputService` maintenance hooks run
only between foreground operations. Render completion remains synchronous:
an obsolete, deferred or failed page cannot advance a bookmark, and only a
successful physical refresh commits the partial-refresh debt model. R1.4
replaces count-based cleanup with [spatial physics observations and debt](DISPLAY_PHYSICS.md);
its recorder stays on this same foreground owner. Existing
`Step` callers retain their behavior; [SDK_V1.md](SDK_V1.md) describes the
additive API and callback-reentry rejection.

The same reentry protection covers factories, application destruction and
shutdown/failsafe delegates. Nested runtime operations return `InvalidState`
until the callback returns; only entering or active application callbacks can
submit navigation or rendering. This closes a recursive-shutdown failure in
the initial E1.3 implementation without adding a mutex or another task.

## Bounded input storage

`ZectrixButtonBuffer` stores 16 physical events. A short board critical section
only copies/shifts those fixed entries; it contains no GPIO, RTOS wait,
callback or display work. A separate one-byte, one-slot FreeRTOS queue wakes
the consumer. Full wake queues already signal runnable work, so the producer
always uses a zero-timeout send. Wake coalescing never occupies a physical
event slot. Both board and service waits preserve their original timeout.

| Incoming event when full | Behavior |
| --- | --- |
| UP/DOWN click | Drop the new direction. |
| OK click | Replace the newest queued direction; if none exists, drop the new confirmation. |
| Long OK / Back | Replace the newest direction, or the newest click if all entries are controls. If all entries are already Back, retain them. |
| Long DOWN / shutdown | Supersede all queued actions with one shutdown; reject other input until it is consumed. |

Retained events keep their order except for shutdown's explicit priority.
Overload is bounded and can lose navigation or excess confirmations; it does
not block the sampler or lose Back/shutdown behind a full direction queue.
Teardown still joins the button task and stops service consumers before
deleting its notification queue.

## Driver failure recovery

Driver mutex acquisition is limited to 100 ms, returning `ESP_ERR_TIMEOUT`
instead of waiting forever. BUSY polling yields at least one tick. Existing
normal-phase limits remain 2 seconds by default; external grayscale refreshes
have a 5-second limit per phase. These are phase limits, not a deadline for an
entire multi-pass grayscale image.

After a gray failure the driver stops commands and marks the controller and
1bpp shadow unusable. Cleanup cuts the external rail without sending `0x02`,
resetting or restoring OTP on a potentially BUSY controller. Ending an explicit
power batch performs the same cleanup. The next successful 1bpp submission
restores a full frame before partial updates. Successful white preclear,
waveforms, OTP restoration and adaptive ghosting limits are preserved.

## Verification and measurement

`tools/test-display-service.sh` runs the real runtime, Launcher controller,
canvas, DisplayService and SSD2683 driver against simulated GPIO/SPI/time.
Nine DOWN clicks arrive during an initial refresh with an 800 ms BUSY period.
The resulting Home canvas and selected tile are identical in both runs:

| Following the initial refresh | Per-event rendering | Bounded coalescing |
| --- | ---: | ---: |
| Physical refresh calls to drain nine inputs | 9 | 1 |
| Simulated backlog drain time | 8.1 s | 0.9 s |

The simulation includes reset/driver delays and an overlapping status update;
it does not measure real SPI transfer time, panel contrast or physical latency.
The active physical refresh still delays application dispatch until completion
or timeout. Sampling concurrency and eliminating obsolete later draws do not
make the panel's current waveform interruptible.

Host scenarios also exercise a real threaded button producer with a saturated
wake queue, control-event overflow and shutdown priority, bounded dispatch
under continuous input, dirty/Quality merging, transitions and callback
reentry through creation, rendering, destruction and shutdown/failsafe paths,
unchanged SDK call signatures, skipped-page bookmark persistence,
and BUSY timeout at the white preclear and every gray pass with/without an
explicit power batch. Failed refreshes must release power and recover through
a full frame. The standard Host and Full/Minimal firmware regression covers
both configured products.

Completion checks on 2026-09-10 passed all 32 Host targets, runtime ASan/UBSan
and Full/Minimal ESP32-S3 builds. Full firmware is 3,001,712 bytes; Minimal is
554,224 bytes. The connected-device Full flash/boot smoke confirmed the first
Launcher frame, boot confirmation, 12 applications and USB CLI startup.
Physical button latency, contrast and ghosting remain panel measurements.
