#include "probe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_timer.h"
#else
#include <time.h>
#endif

typedef union {
    max_align_t alignment;
    size_t size;
} Allocation;

ProbeHeap probe_heap = {.limit = 256 * 1024};
unsigned probe_emissions;

void* probe_realloc(void* pointer, size_t size) {
    Allocation* old = pointer ? (Allocation*)pointer - 1 : NULL;
    const size_t old_size = old ? old->size : 0;
    if (!size) {
        probe_heap.live -= old_size;
        free(old);
        return NULL;
    }
    if (size > SIZE_MAX - sizeof(Allocation) ||
        size > probe_heap.limit - (probe_heap.live - old_size)) {
        ++probe_heap.rejected;
        return NULL;
    }
    Allocation* allocation = realloc(old, sizeof(Allocation) + size);
    if (!allocation) return NULL;
    allocation->size = size;
    probe_heap.live = probe_heap.live - old_size + size;
    if (probe_heap.live > probe_heap.peak) probe_heap.peak = probe_heap.live;
    return allocation + 1;
}

void* probe_malloc(size_t size) { return probe_realloc(NULL, size); }

void* probe_calloc(size_t count, size_t size) {
    if (size && count > SIZE_MAX / size) return NULL;
    void* pointer = probe_malloc(count * size);
    if (pointer) memset(pointer, 0, count * size);
    return pointer;
}

void probe_free(void* pointer) { (void)probe_realloc(pointer, 0); }

void probe_require(int condition, const char* detail) {
    if (condition) return;
    fprintf(stderr, "Probe failed: %s\n", detail);
    abort();
}

int32_t probe_emit(const void* bytes, uint32_t length) {
    // Copy a bounded host-owned payload before a guest can change its memory.
    if (length > 64) return -1;
    char copy[64];
    memcpy(copy, bytes, length);
    if (length != 5 || memcmp(copy, "NOTE4", 5) != 0) {
        fprintf(stderr, "Unexpected emission %u: length=%u bytes=%02x %02x %02x %02x %02x\n",
                probe_emissions, (unsigned)length, (unsigned char)copy[0],
                (unsigned char)copy[1], (unsigned char)copy[2],
                (unsigned char)copy[3], (unsigned char)copy[4]);
    }
    PROBE_REQUIRE(length == 5 && memcmp(copy, "NOTE4", 5) == 0);
    ++probe_emissions;
    return 0;
}

uint64_t probe_now_us(void) {
#ifdef ESP_PLATFORM
    return (uint64_t)esp_timer_get_time();
#else
    struct timespec now;
    PROBE_REQUIRE(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (uint64_t)now.tv_sec * 1000000 + (uint64_t)now.tv_nsec / 1000;
#endif
}
