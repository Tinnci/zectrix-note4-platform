# Bounded application runtimes

Lua 5.4.9 remains the first-party application runtime and `.zapp` format.
WAMR 2.4.5 and Wasm3 0.5.0 are optional maintained C backends in `note4_wasm.h`;
they do not replace the launcher, Lua UI imports or package format. Select one
in menuconfig: `NOTE4_ENABLE_WASM`, then `NOTE4_WASM_ENGINE_WAMR` or
`NOTE4_WASM_ENGINE_WASM3`. Default is off to avoid shipping an unused engine.
`NOTE4_WASM_SOURCE_DIR` can point at an unmodified upstream release for offline builds.

```c
Note4WasmOptions options = {10000, 128 * 1024, emit, context};
if (note4_wasm_start(module, module_bytes, options)) {
    int32_t result;
    if (!note4_wasm_call("initialize", &result)) { /* owner handles failure */ }
    note4_wasm_stop();
}
```

One foreground owner, one active guest per process, no engine task or shared
ownership. Source bytes are copied until stop. Heap quotas include allocator
headers. Failed start/stop releases tracked memory. Calls support `() -> i32`
exports and leave results unchanged on failure.

Limits: 32 KiB module, one non-shared defined memory with explicit maximum one
64 KiB page, 4 KiB interpreter stack, 4–128 KiB heap including engine objects,
bytecode and linear memory. Tracked ESP32 allocations use PSRAM; the owner's
native stack and SDK synchronization overhead are outside this heap quota.
No WASI, standard
libraries, JIT/AOT, threads or hardware imports. The sole import,
`note4_v1.emit(offset,length)->i32`, copies at most 64 bytes before a bounded,
nonblocking callback. Callbacks must not reenter/destroy the engine. Instruction
budgets cannot preempt native callbacks.

Start sections and automatic constructors are rejected before instantiation;
WAMR otherwise executes `__post_instantiate` before the call budget exists.
The owner calls explicit `initialize` under its normal budget. WAMR counts
interpreter instructions; patched Wasm3 counts dispatches including branches
and backedges. These units differ from Lua hooks and wall-clock deadlines.
Parsing is input/heap bounded; S3 worst-case latency still needs measurement.

Tracked patches apply only to build-owned copies. Wasm3 fixes its visitor
callback type and unmetered loop dispatch. Its code-page and constant-table access use
`memcpy`, avoiding type-punned stores under GCC 15 without disabling
strict-aliasing warnings. WAMR fixes frame/branch alignment on 64-bit hosts;
32-bit Xtensa alignment stays unchanged. Native stack checks use the actual
compiler frame rather than ASan fake-stack local addresses. ASan/UBSan and
use-after-return checks are not disabled. Its ESP-IDF port explicitly includes
`sys/stat.h`; unused WASI file/socket adapters are excluded from the firmware build, matching the
disabled guest capabilities on both SDK lanes. Upstream licenses remain in
copied sources.

Qualification: `uv run tools/runtime-qualification/run.py --fetch --sanitize`.
After activating ESP-IDF, `--idf` links the same production adapter into isolated
Full images without executing/flashing it. Tests cover ownership, cleanup,
loops, initialization, signatures, copied imports, heap and image size. Host
correctness and S3 linking do not establish device stack safety, power cost or
runtime cancellation latency.
