#include "probe.h"
#include "guest.h"

#include <stdio.h>
#include <string.h>

static const uint8_t* source_bytes = guest_bytes;
static uint32_t source_size = sizeof(guest_bytes);

#ifdef PROBE_WAMR
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
    return probe_emit(wasm_runtime_addr_app_to_native(owner, offset), length);
}

static void* wamr_malloc(mem_alloc_usage_t usage, unsigned size) {
    (void)usage;
    return probe_malloc(size);
}
static void* wamr_realloc(mem_alloc_usage_t usage, bool mapped, void* pointer, unsigned size) {
    (void)usage;
    (void)mapped;
    return probe_realloc(pointer, size);
}
static void wamr_free(mem_alloc_usage_t usage, void* pointer) {
    (void)usage;
    probe_free(pointer);
}

static int open_guest(void) {
    static NativeSymbol imports[] = {{"emit", (void*)emit, "(ii)i", NULL}};
    RuntimeInitArgs args = {0};
    args.mem_alloc_type = Alloc_With_Allocator;
    args.mem_alloc_option.allocator.malloc_func = (void*)wamr_malloc;
    args.mem_alloc_option.allocator.realloc_func = (void*)wamr_realloc;
    args.mem_alloc_option.allocator.free_func = (void*)wamr_free;
    args.native_module_name = "note4_probe";
    args.native_symbols = imports;
    args.n_native_symbols = 1;
    initialized = wasm_runtime_full_init(&args);
    if (!initialized) return 0;
    // WAMR can rewrite bytecode while loading; the backing bytes stay owned.
    module_bytes = probe_malloc(source_size);
    if (!module_bytes) return 0;
    memcpy(module_bytes, source_bytes, source_size);
    module = wasm_runtime_load(module_bytes, source_size, error, sizeof(error));
    if (!module) return 0;
    instance = wasm_runtime_instantiate(module, 4096, 0, error, sizeof(error));
    if (!instance) return 0;
    execution = wasm_runtime_create_exec_env(instance, 4096);
    return execution != NULL;
}

static int call_guest(const char* name, int32_t* result) {
    wasm_function_inst_t function = wasm_runtime_lookup_function(instance, name);
    PROBE_REQUIRE(function != NULL);
    wasm_runtime_clear_exception(instance);
    wasm_runtime_set_instruction_count_limit(execution, 10000);
    uint32_t value = 0;
    const int ok = wasm_runtime_call_wasm(execution, function, 0, &value);
    *result = (int32_t)value;
    return ok;
}

static const char* last_error(void) { return wasm_runtime_get_exception(instance); }

static void close_guest(void) {
    if (execution) wasm_runtime_destroy_exec_env(execution);
    if (instance) wasm_runtime_deinstantiate(instance);
    if (module) wasm_runtime_unload(module);
    probe_free(module_bytes);
    if (initialized) wasm_runtime_destroy();
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

// This hook is deliberately armed during spin. It cannot see loop backedges.
M3Result m3_Yield(void) {
    return ++yield_calls > 100 ? "probe budget exceeded" : m3Err_none;
}

m3ApiRawFunction(emit) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, offset);
    m3ApiGetArg(uint32_t, length);
    (void)_ctx;
    (void)_mem;
    uint32_t size = 0;
    uint8_t* memory = m3_GetMemory(runtime, &size, 0);
    if (length > 64 || offset > size || length > size - offset) m3ApiReturn(-1);
    m3ApiReturn(probe_emit(memory + offset, length));
}

static int open_guest(void) {
    // Module initialization also calls m3_Yield while evaluating data offsets.
    yield_calls = 0;
    environment = m3_NewEnvironment();
    if (!environment) return 0;
    runtime = m3_NewRuntime(environment, 4096, NULL);
    if (!runtime) return 0;
    error = m3_ParseModule(environment, &unloaded_module, source_bytes, source_size);
    if (error) return 0;
    error = m3_LoadModule(runtime, unloaded_module);
    if (error) return 0;
    IM3Module loaded = unloaded_module;
    unloaded_module = NULL;
    error = m3_LinkRawFunction(loaded, "note4_probe", "emit", "i(ii)", emit);
    return error == NULL;
}

static int call_guest(const char* name, int32_t* result) {
    IM3Function function = NULL;
    yield_calls = 0;
    error = m3_FindFunction(&function, runtime, name);
    if (!error) error = m3_CallV(function);
    if (!error) error = m3_GetResultsV(function, result);
    return error == NULL;
}

static const char* last_error(void) { return error; }

static void close_guest(void) {
    if (unloaded_module) m3_FreeModule(unloaded_module);
    if (runtime) m3_FreeRuntime(runtime);
    if (environment) m3_FreeEnvironment(environment);
    unloaded_module = NULL;
    runtime = NULL;
    environment = NULL;
}

#define ENGINE "wasm3"
#endif

int research_probe(ProbeMode mode) {
    int32_t result = 0;
    if (mode == PROBE_START || mode == PROBE_POST) {
        source_bytes = mode == PROBE_START ? guest_start_bytes : guest_post_bytes;
        source_size = mode == PROBE_START ? sizeof(guest_start_bytes) : sizeof(guest_post_bytes);
        puts("{\"event\":\"init-start\"}");
        fflush(stdout);
        PROBE_REQUIRE(open_guest());
        // Wasm3 defers the start function until the first function lookup.
        PROBE_REQUIRE(call_guest("step", &result));
        close_guest();
        puts("{\"event\":\"init-returned\"}");
        return 0;
    }
    PROBE_REQUIRE(open_guest());
    if (mode == PROBE_SPIN) {
        puts("{\"event\":\"spin-start\"}");
        fflush(stdout);
        PROBE_REQUIRE(!call_guest("spin", &result));
#ifdef PROBE_WAMR
        PROBE_REQUIRE(strstr(last_error(), "instruction limit") != NULL);
#endif
        close_guest();
        PROBE_REQUIRE(probe_heap.live == 0);
        puts("{\"event\":\"spin-trapped\"}");
        return 0;
    }

    PROBE_REQUIRE(call_guest("step", &result) && result == 2080);
    const uint64_t start = probe_now_us();
    for (unsigned i = 0; i < 10000; ++i) {
        PROBE_REQUIRE(call_guest("step", &result) && result == 2080);
    }
    const uint64_t elapsed = probe_now_us() - start;
    PROBE_REQUIRE(probe_emissions == 10001);
    const unsigned emitted = probe_emissions;
    PROBE_REQUIRE(call_guest("bad_import", &result) && result == -1);
    PROBE_REQUIRE(call_guest("long_import", &result) && result == -1);
    PROBE_REQUIRE(probe_emissions == emitted);
    PROBE_REQUIRE(call_guest("grow", &result) && result == -1);
    PROBE_REQUIRE(!call_guest("out_of_bounds", &result));
    PROBE_REQUIRE(strstr(last_error(), "out of bounds") != NULL);
    PROBE_REQUIRE(!call_guest("recurse", &result));
    close_guest();
    PROBE_REQUIRE(probe_heap.live == 0);

    // Reject a truncated module and release all partially created state.
    source_size = 7;
    PROBE_REQUIRE(!open_guest());
    close_guest();
    PROBE_REQUIRE(probe_heap.live == 0);
    source_size = sizeof(guest_bytes);

    // Repeat the complete owner lifetime, including after a trapped callback.
    for (unsigned i = 0; i < 100; ++i) {
        PROBE_REQUIRE(open_guest());
        PROBE_REQUIRE(call_guest("step", &result) && result == 2080);
        close_guest();
        PROBE_REQUIRE(probe_heap.live == 0);
    }
    const size_t normal_peak = probe_heap.peak;
    probe_heap.limit = 32 * 1024;
    PROBE_REQUIRE(!open_guest());
    close_guest();
    PROBE_REQUIRE(probe_heap.rejected > 0 && probe_heap.live == 0);
    printf("{\"engine\":\"%s\",\"normal_peak_bytes\":%zu,\"live_after_close\":%zu,"
           "\"allocation_rejections\":%zu,\"step_10000_us\":%llu,\"cycles\":100}\n",
           ENGINE, normal_peak, probe_heap.live, probe_heap.rejected,
           (unsigned long long)elapsed);
    return 0;
}
