#include "note4_wasm.h"
#include "note4_wasm_admission.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "note4_log_event.h"
#define RUNTIME_EVENT(level, event, fields, ...)                                                   \
    NOTE4_LOG##level("runtime", event, fields, ##__VA_ARGS__)
#else
#define RUNTIME_EVENT(level, event, fields, ...) ((void)0)
#endif

static const uint8_t* source_bytes;
static uint8_t* owned_bytes;
static uint32_t source_size, instruction_limit;
static Note4WasmHeap heap;
static size_t heap_limit;
static Note4WasmEmit callback;
static void* callback_context;
static char detail[128];
static int running, in_callback;
static void SetError(const char* text) {
    size_t n = 0;
    while (text && text[n] && n < sizeof(detail) - 1) {
        const unsigned char c = text[n];
        detail[n] = c >= 32 && c < 127 ? c : '?';
        ++n;
    }
    detail[n] = 0;
}
typedef union {
    max_align_t align;
    size_t bytes;
} Allocation;
void* note4_wasm_realloc(void* pointer, size_t size) {
    Allocation* old = pointer ? (Allocation*)pointer - 1 : NULL;
    const size_t previous = old ? old->bytes : 0;
    if (!size) {
        heap.live -= previous;
#ifdef ESP_PLATFORM
        heap_caps_free(old);
#else
        free(old);
#endif
        return NULL;
    }
    if (size > SIZE_MAX - sizeof(Allocation) ||
        size + sizeof(Allocation) > heap_limit - (heap.live - previous)) {
        ++heap.rejected;
        return NULL;
    }
    const size_t bytes = size + sizeof(Allocation);
#ifdef ESP_PLATFORM
    Allocation* block = heap_caps_realloc(old, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    Allocation* block = realloc(old, bytes);
#endif
    if (!block) {
        ++heap.rejected;
        return NULL;
    }
    block->bytes = bytes;
    heap.live = heap.live - previous + bytes;
    if (heap.live > heap.peak)
        heap.peak = heap.live;
    return block + 1;
}
void* note4_wasm_malloc(size_t size) { return note4_wasm_realloc(NULL, size); }
void note4_wasm_free(void* pointer) { (void)note4_wasm_realloc(pointer, 0); }
void* note4_wasm_calloc(size_t count, size_t size) {
    if (size && count > SIZE_MAX / size)
        return NULL;
    void* p = note4_wasm_malloc(count * size);
    if (p)
        memset(p, 0, count * size);
    return p;
}
static int32_t Deliver(const void* bytes, uint32_t size) {
    if (!callback || size > 64)
        return -1;
    uint8_t copy[64] = {0};
    if (size)
        memcpy(copy, bytes, size);
    in_callback = 1;
    const int32_t result = callback(callback_context, copy, size);
    in_callback = 0;
    return result;
}
#ifdef NOTE4_WASM_WAMR
#include "wasm_export.h"

static wasm_module_t module;
static wasm_module_inst_t instance;
static wasm_exec_env_t execution;
static int initialized;
static char error[128];
static uint8_t* module_bytes;

static int32_t emit(wasm_exec_env_t env, uint32_t offset, uint32_t length) {
    wasm_module_inst_t owner = wasm_runtime_get_module_inst(env);
    if (length > 64 || !wasm_runtime_validate_app_addr(owner, offset, length)) {
        // A rejected import is an ordinary host error, not a guest memory access.
        wasm_runtime_clear_exception(owner);
        return -1;
    }
    return Deliver(wasm_runtime_addr_app_to_native(owner, offset), length);
}

static void* wamr_malloc(mem_alloc_usage_t usage, unsigned size) {
    (void)usage;
    return note4_wasm_malloc(size);
}
static void* wamr_realloc(mem_alloc_usage_t usage, bool mapped, void* pointer, unsigned size) {
    (void)usage;
    (void)mapped;
    return note4_wasm_realloc(pointer, size);
}
static void wamr_free(mem_alloc_usage_t usage, void* pointer) {
    (void)usage;
    note4_wasm_free(pointer);
}

static int open_guest(void) {
    if (note4_wasm_admit(source_bytes, source_size) != NOTE4_WASM_ACCEPT)
        return 0;
    static NativeSymbol imports[] = {{"emit", (void*)emit, "(ii)i", NULL}};
    RuntimeInitArgs args = {0};
    args.mem_alloc_type = Alloc_With_Allocator;
    args.mem_alloc_option.allocator.malloc_func = (void*)wamr_malloc;
    args.mem_alloc_option.allocator.realloc_func = (void*)wamr_realloc;
    args.mem_alloc_option.allocator.free_func = (void*)wamr_free;
    args.native_module_name = "note4_v1";
    args.native_symbols = imports;
    args.n_native_symbols = 1;
    initialized = wasm_runtime_full_init(&args);
    if (!initialized)
        return 0;
    // WAMR can rewrite bytecode while loading; the backing bytes stay owned.
    module_bytes = owned_bytes;
    module = wasm_runtime_load(module_bytes, source_size, error, sizeof(error));
    if (!module)
        return 0;
    instance = wasm_runtime_instantiate(module, 4096, 0, error, sizeof(error));
    if (!instance)
        return 0;
    execution = wasm_runtime_create_exec_env(instance, 4096);
    return execution != NULL;
}

static int call_guest(const char* name, int32_t* result) {
    wasm_function_inst_t function = wasm_runtime_lookup_function(instance, name);
    if (!function) {
        SetError("missing export");
        return 0;
    }
    if (wasm_func_get_param_count(function, instance) ||
        wasm_func_get_result_count(function, instance) != 1) {
        SetError("unsupported export signature");
        return 0;
    }
    wasm_valkind_t type;
    wasm_func_get_result_types(function, instance, &type);
    if (type != WASM_I32) {
        SetError("unsupported result type");
        return 0;
    }
    wasm_runtime_clear_exception(instance);
    wasm_runtime_set_instruction_count_limit(execution, instruction_limit);
    uint32_t value = 0;
    const int ok = wasm_runtime_call_wasm(execution, function, 0, &value);
    *result = (int32_t)value;
    return ok;
}

static const char* last_error(void) { return wasm_runtime_get_exception(instance); }

static void close_guest(void) {
    if (execution)
        wasm_runtime_destroy_exec_env(execution);
    if (instance)
        wasm_runtime_deinstantiate(instance);
    if (module)
        wasm_runtime_unload(module);
    if (initialized)
        wasm_runtime_destroy();
    execution = NULL;
    instance = NULL;
    module = NULL;
    module_bytes = NULL;
    initialized = 0;
}

#define ENGINE "wamr"
#else
#include "wasm3.h"

static IM3Environment environment;
static IM3Runtime runtime;
static IM3Module unloaded_module;
static M3Result error;
static unsigned yield_calls;

// The maintained dispatch patch covers branches and backedges, not just calls.
M3Result m3_Yield(void) {
    return ++yield_calls > instruction_limit ? "instruction limit" : m3Err_none;
}

m3ApiRawFunction(emit) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, offset);
    m3ApiGetArg(uint32_t, length);
    (void)_ctx;
    (void)_mem;
    uint32_t size = 0;
    uint8_t* memory = m3_GetMemory(runtime, &size, 0);
    if (!memory || length > 64 || offset > size || length > size - offset)
        m3ApiReturn(-1);
    m3ApiReturn(Deliver(memory + offset, length));
}

static int open_guest(void) {
    if (note4_wasm_admit(source_bytes, source_size) != NOTE4_WASM_ACCEPT)
        return 0;
    // Module initialization also calls m3_Yield while evaluating data offsets.
    yield_calls = 0;
    environment = m3_NewEnvironment();
    if (!environment)
        return 0;
    runtime = m3_NewRuntime(environment, 4096, NULL);
    if (!runtime)
        return 0;
    error = m3_ParseModule(environment, &unloaded_module, source_bytes, source_size);
    if (error)
        return 0;
    error = m3_LoadModule(runtime, unloaded_module);
    if (error)
        return 0;
    IM3Module loaded = unloaded_module;
    unloaded_module = NULL;
    error = m3_LinkRawFunction(loaded, "note4_v1", "emit", "i(ii)", emit);
    // Pure-compute modules need not import the host interface.
    if (error == m3Err_functionLookupFailed)
        error = NULL;
    return error == NULL;
}

static int call_guest(const char* name, int32_t* result) {
    IM3Function function = NULL;
    yield_calls = 0;
    error = m3_FindFunction(&function, runtime, name);
    if (!error && (m3_GetArgCount(function) || m3_GetRetCount(function) != 1 ||
                   m3_GetRetType(function, 0) != c_m3Type_i32))
        error = "unsupported export signature";
    if (!error)
        error = m3_CallV(function);
    if (!error)
        error = m3_GetResultsV(function, result);
    return error == NULL;
}

static const char* last_error(void) { return error; }

static void close_guest(void) {
    if (unloaded_module)
        m3_FreeModule(unloaded_module);
    if (runtime)
        m3_FreeRuntime(runtime);
    if (environment)
        m3_FreeEnvironment(environment);
    unloaded_module = NULL;
    runtime = NULL;
    environment = NULL;
}

#define ENGINE "wasm3"
#endif

void note4_wasm_stop(void) {
    if (in_callback)
        return; // No destruction/reentrant engine calls from imports.
    const int was_running = running;
    close_guest();
    note4_wasm_free(owned_bytes);
    owned_bytes = NULL;
    source_bytes = NULL;
    source_size = 0;
    callback = NULL;
    callback_context = NULL;
    running = 0;
    if (was_running)
        RUNTIME_EVENT(D, "wasm_stopped", "peak_bytes=%lu live_bytes=%lu rejected=%lu",
                      (unsigned long)heap.peak, (unsigned long)heap.live,
                      (unsigned long)heap.rejected);
}
int note4_wasm_start(const uint8_t* bytes, size_t size, Note4WasmOptions options) {
    if (running || in_callback) {
        SetError("runtime busy");
        return 0;
    }
    note4_wasm_stop();
    heap = (Note4WasmHeap){0};
    SetError(NULL);
    if (options.instructions < 100 || options.instructions > 10000 || options.heap_bytes < 4096 ||
        options.heap_bytes > 128 * 1024) {
        SetError("invalid quota");
        return 0;
    }
    if (note4_wasm_admit(bytes, size) != NOTE4_WASM_ACCEPT) {
        SetError("invalid module or automatic initialization");
        RUNTIME_EVENT(W, "wasm_rejected", "stage=admission");
        return 0;
    }
    instruction_limit = options.instructions;
    heap_limit = options.heap_bytes;
    callback = options.emit;
    callback_context = options.context;
    owned_bytes = note4_wasm_malloc(size);
    if (!owned_bytes) {
        SetError("memory limit");
        note4_wasm_stop();
        return 0;
    }
    memcpy(owned_bytes, bytes, size);
    source_bytes = owned_bytes;
    source_size = size;
    if (!open_guest()) {
        SetError("module load or memory limit");
        note4_wasm_stop();
        RUNTIME_EVENT(W, "wasm_rejected", "stage=load");
        return 0;
    }
    running = 1;
    RUNTIME_EVENT(I, "wasm_started", "engine=%s module_bytes=%lu heap_limit=%lu budget=%lu", ENGINE,
                  (unsigned long)size, (unsigned long)heap_limit, (unsigned long)instruction_limit);
    return 1;
}
int note4_wasm_call(const char* name, int32_t* result) {
    if (!running || in_callback || !name || !result)
        return 0;
    size_t length = 0;
    while (name[length] && length < 64) {
        const char c = name[length++];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_'))
            return 0;
    }
    if (!length || length == 64)
        return 0;
    SetError(NULL);
    int32_t value = 0;
    if (!call_guest(name, &value)) {
        if (!detail[0])
            SetError(last_error());
        RUNTIME_EVENT(W, "wasm_call_failed", "engine=%s", ENGINE);
        return 0;
    }
    *result = value;
    return 1;
}
const char* note4_wasm_error(void) { return detail; }
const char* note4_wasm_engine(void) { return ENGINE; }
Note4WasmHeap note4_wasm_heap(void) { return heap; }
