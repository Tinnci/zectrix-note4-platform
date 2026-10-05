#include "note4_wasm_admission.h"
#include <assert.h>
#include <string.h>

static const uint8_t header[] = {0, 97, 115, 109, 1, 0, 0, 0};
static uint8_t module[32769];
static size_t length;
static void reset(void) {
    memcpy(module, header, sizeof(header));
    length = sizeof(header);
}
static void section(uint8_t kind, const uint8_t* bytes, size_t count) {
    assert(count < 128);
    module[length++] = kind;
    module[length++] = (uint8_t)count;
    memcpy(module + length, bytes, count);
    length += count;
}
static void expect(note4_wasm_admission_t result) {
    assert(note4_wasm_admit(module, length) == result);
}
int main(void) {
    assert(note4_wasm_admit(NULL, 0) == NOTE4_WASM_INVALID);
    reset();
    expect(NOTE4_WASM_ACCEPT);
    const uint8_t imports[] = {1,   8, 'n', 'o', 't', 'e', '4', '_', 'v',
                               '1', 4, 'e', 'm', 'i', 't', 0,   0};
    section(2, imports, sizeof(imports));
    expect(NOTE4_WASM_ACCEPT);
    module[length - 2] = 2;
    expect(NOTE4_WASM_INVALID);
    reset();
    section(2, imports, sizeof(imports));
    module[12] = 'X';
    expect(NOTE4_WASM_INVALID);
    reset();
    for (size_t size = 0; size < 8; ++size)
        assert(note4_wasm_admit(module, size) == NOTE4_WASM_INVALID);
    const uint8_t memory[] = {1, 1, 1, 1};
    section(5, memory, sizeof(memory));
    expect(NOTE4_WASM_ACCEPT);
    section(5, memory, sizeof(memory));
    expect(NOTE4_WASM_INVALID);
    const uint8_t bad_memory[][4] = {
        {1, 0, 1, 1}, {1, 3, 1, 1}, {1, 1, 2, 2}, {2, 1, 1, 1}, {1, 1, 1, 2}};
    for (size_t i = 0; i < sizeof(bad_memory) / sizeof(bad_memory[0]); ++i) {
        reset();
        section(5, bad_memory[i], 4);
        expect(NOTE4_WASM_INVALID);
    }
    reset();
    const uint8_t start[] = {0};
    section(8, start, 1);
    expect(NOTE4_WASM_AUTO_INIT);
    const char* names[] = {"__post_instantiate", "__wasm_call_ctors", "initialize"};
    for (unsigned i = 0; i < 3; ++i) {
        uint8_t exports[64] = {1};
        const size_t n = strlen(names[i]);
        exports[1] = (uint8_t)n;
        memcpy(exports + 2, names[i], n);
        reset();
        section(7, exports, n + 4);
        expect(i < 2 ? NOTE4_WASM_AUTO_INIT : NOTE4_WASM_ACCEPT);
        // Constructor-named globals cannot execute automatically.
        exports[n + 2] = 3;
        reset();
        section(7, exports, n + 4);
        expect(NOTE4_WASM_ACCEPT);
    }
    reset();
    section(5, memory, sizeof(memory));
    const uint8_t exports[] = {1, 4, 's', 't', 'e', 'p', 0, 0};
    section(7, exports, sizeof(exports));
    const size_t complete = length;
    for (size_t i = 0; i < complete; ++i) {
        for (unsigned bit = 0; bit < 8; ++bit) {
            module[i] ^= (uint8_t)(1U << bit);
            const note4_wasm_admission_t status = note4_wasm_admit(module, complete);
            assert(status >= NOTE4_WASM_ACCEPT && status <= NOTE4_WASM_AUTO_INIT);
            module[i] ^= (uint8_t)(1U << bit);
        }
        (void)note4_wasm_admit(module, i);
    }
    reset();
    module[length++] = 0;
    memset(module + length, 0xff, 5);
    length += 5;
    expect(NOTE4_WASM_INVALID);
    reset();
    module[length++] = 0;
    module[length++] = 0x80;
    module[length++] = 0x80;
    module[length++] = 0x80;
    module[length++] = 0x80;
    module[length++] = 0x10;
    expect(NOTE4_WASM_INVALID);
    reset();
    section(13, start, 1);
    expect(NOTE4_WASM_INVALID);
    reset();
    memset(module + length, 0, sizeof(module) - length);
    assert(note4_wasm_admit(module, sizeof(module)) == NOTE4_WASM_INVALID);
    // Bounded malformed input corpus: exercise all reader branches under ASan.
    uint32_t random = 0x13579;
    for (unsigned round = 0; round < 10000; ++round) {
        reset();
        const size_t count = round % 256;
        for (size_t i = 0; i < count; ++i) {
            random = random * 1664525U + 1013904223U;
            module[length++] = (uint8_t)(random >> 24);
        }
        (void)note4_wasm_admit(module, length);
    }
    return 0;
}
