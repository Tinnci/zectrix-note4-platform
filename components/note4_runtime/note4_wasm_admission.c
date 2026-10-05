#include "note4_wasm_admission.h"
#include <string.h>

typedef struct {
    const uint8_t* bytes;
    size_t size, cursor;
} Reader;
static int leb(Reader* r, uint32_t* value) {
    *value = 0;
    for (unsigned n = 0; n < 5; ++n) {
        if (r->cursor == r->size)
            return 0;
        const uint8_t b = r->bytes[r->cursor++];
        if (n == 4 && (b & 0xf0))
            return 0;
        *value |= (uint32_t)(b & 0x7f) << (n * 7);
        if (!(b & 0x80))
            return 1;
    }
    return 0;
}
static int literal(Reader* reader, const char* expected) {
    uint32_t size;
    if (!leb(reader, &size) || size > reader->size - reader->cursor || size != strlen(expected) ||
        memcmp(reader->bytes + reader->cursor, expected, size))
        return 0;
    reader->cursor += size;
    return 1;
}
note4_wasm_admission_t note4_wasm_admit(const uint8_t* bytes, size_t size) {
    static const uint8_t header[] = {0, 97, 115, 109, 1, 0, 0, 0};
    if (!bytes || size < 8 || size > 32768 || memcmp(bytes, header, 8))
        return NOTE4_WASM_INVALID;
    Reader file = {bytes, size, 8};
    unsigned seen = 0;
    while (file.cursor < size) {
        const unsigned section = bytes[file.cursor++];
        uint32_t length;
        if (section > 12 || !leb(&file, &length) || length > size - file.cursor)
            return NOTE4_WASM_INVALID;
        if (section && (seen & (1U << section)))
            return NOTE4_WASM_INVALID;
        if (section)
            seen |= 1U << section;
        if (section == 8)
            return NOTE4_WASM_AUTO_INIT;
        if (section == 2) {
            Reader imports = {bytes + file.cursor, length, 0};
            uint32_t count, type;
            if (!leb(&imports, &count) || count > 1)
                return NOTE4_WASM_INVALID;
            if (count && (!literal(&imports, "note4_v1") || !literal(&imports, "emit") ||
                          imports.cursor == imports.size || imports.bytes[imports.cursor++] != 0 ||
                          !leb(&imports, &type)))
                return NOTE4_WASM_INVALID;
            // No imported memories/tables/globals, shared memory or extra host symbols.
            if (imports.cursor != imports.size)
                return NOTE4_WASM_INVALID;
        }
        if (section == 5) {
            Reader memory = {bytes + file.cursor, length, 0};
            uint32_t count, flags, minimum, maximum;
            // Both backends expose the same fixed, non-shared 64KiB memory.
            // Explicit maximum prevents growth from relying on allocator OOM.
            if (!leb(&memory, &count) || count > 1)
                return NOTE4_WASM_INVALID;
            if (count && (!leb(&memory, &flags) || flags != 1 || !leb(&memory, &minimum) ||
                          minimum > 1 || !leb(&memory, &maximum) || maximum != 1))
                return NOTE4_WASM_INVALID;
            if (memory.cursor != memory.size)
                return NOTE4_WASM_INVALID;
        }
        if (section == 7) {
            Reader exports = {bytes + file.cursor, length, 0};
            uint32_t count;
            if (!leb(&exports, &count) || count > length)
                return NOTE4_WASM_INVALID;
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t name_size, index;
                if (!leb(&exports, &name_size) || name_size >= exports.size - exports.cursor)
                    return NOTE4_WASM_INVALID;
                const uint8_t* name = exports.bytes + exports.cursor;
                exports.cursor += name_size;
                const unsigned kind = exports.bytes[exports.cursor++];
                if (kind > 3 || !leb(&exports, &index))
                    return NOTE4_WASM_INVALID;
                if (kind == 0 && ((name_size == 18 && !memcmp(name, "__post_instantiate", 18)) ||
                                  (name_size == 17 && !memcmp(name, "__wasm_call_ctors", 17))))
                    return NOTE4_WASM_AUTO_INIT;
            }
            if (exports.cursor != exports.size)
                return NOTE4_WASM_INVALID;
        }
        file.cursor += length;
    }
    return NOTE4_WASM_ACCEPT;
}
