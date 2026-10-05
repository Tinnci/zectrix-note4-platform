#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Single foreground owner, one active guest per process. No engine-owned tasks.
// Imports must be bounded, nonblocking and must not reenter any runtime API.
typedef int32_t (*Note4WasmEmit)(void* context, const uint8_t* bytes, uint32_t size);
typedef struct {
    size_t live, peak, rejected;
} Note4WasmHeap;
typedef struct {
    uint32_t instructions;
    size_t heap_bytes;
    Note4WasmEmit emit;
    void* context;
} Note4WasmOptions;
// The runtime owns a mutable copy; callers may release source bytes after start.
// No start section or implicit constructors; invoke an explicit init export.
// Host ABI note4_v1.emit(i32 offset,i32 length)->i32, maximum copied payload 64B.
// At most one defined memory: explicit maximum 1 page (64KiB), not shared.
int note4_wasm_start(const uint8_t* bytes, size_t size, Note4WasmOptions options);
// Only ()->i32 exports are supported; failures leave the result unchanged.
int note4_wasm_call(const char* export_name, int32_t* result);
void note4_wasm_stop(void);
const char* note4_wasm_error(void);
const char* note4_wasm_engine(void);
Note4WasmHeap note4_wasm_heap(void);
// Allocation hooks for the selected upstream engine, not guest imports.
void* note4_wasm_malloc(size_t size);
void* note4_wasm_calloc(size_t count, size_t size);
void* note4_wasm_realloc(void* pointer, size_t size);
void note4_wasm_free(void* pointer);
#ifdef __cplusplus
}
#endif
