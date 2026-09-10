#pragma once

#include <stddef.h>
#include <stdint.h>

// Research-only accounting counts requested bytes, excluding allocator headers.
typedef struct {
    size_t live;
    size_t peak;
    size_t limit;
    size_t rejected;
} ProbeHeap;

extern ProbeHeap probe_heap;
extern unsigned probe_emissions;

void* probe_malloc(size_t size);
void* probe_calloc(size_t count, size_t size);
void* probe_realloc(void* pointer, size_t size);
void probe_free(void* pointer);
void probe_require(int condition, const char* detail);
int32_t probe_emit(const void* bytes, uint32_t length);
uint64_t probe_now_us(void);
int research_probe(int spin_only);

#define PROBE_REQUIRE(condition) probe_require((condition), #condition)
