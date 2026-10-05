# Runtime qualification

Execute the maintained Lua / WAMR / Wasm3 adapters, not a VM simulation.

```sh
uv run tools/runtime-qualification/run.py --fetch
uv run tools/runtime-qualification/run.py --sanitize
source tools/activate-dev-env.sh
uv run tools/runtime-qualification/run.py --idf --engines wamr wasm3
```

Release sources and patched copies stay in ignored `build-runtime-qualification`.
`--sources` selects another release directory; `--output` isolates a run. Every
concurrent invocation should use its own output directory; normal and sanitizer
engine copies are also separated to avoid applying patches over another build.
Every engine must finish normal, loop and initialization cases: process timeout is a
failure, never success. The normal host suite tests the admission parser without
fetching engines. Full engine qualification runs separately.

Wasm accounting includes allocator headers and owned source bytes. Lua's
qualification allocator counts requested payload only; do not compare these as
identical memory accounting. Timing units and sanitizer overhead also differ.
`--idf` retains real backend calls in a Full image for link-size measurement;
hostile fixtures are never executed on the physical device. See the
[runtime API](../../components/note4_runtime/README.md) for limits.
