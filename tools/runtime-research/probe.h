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

typedef enum { PROBE_NORMAL, PROBE_SPIN, PROBE_START, PROBE_POST } ProbeMode;

void* probe_malloc(size_t size);
void* probe_calloc(size_t count, size_t size);
void* probe_realloc(void* pointer, size_t size);
void probe_free(void* pointer);
void probe_require(int condition, const char* detail, const char* file, int line);
int32_t probe_emit(const void* bytes, uint32_t length);
uint64_t probe_now_us(void);
int research_probe(ProbeMode mode);

#define PROBE_REQUIRE(condition) probe_require((condition), #condition, __FILE__, __LINE__)
