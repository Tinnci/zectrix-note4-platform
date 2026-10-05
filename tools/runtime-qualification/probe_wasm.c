#include "guest.h"
#include "note4_wasm.h"
#include "note4_wasm_admission.h"
#include "probe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(NOTE4_WASM_WAMR) && !defined(ESP_PLATFORM)
#include "wasm_export.h"
static void NativeStackChecks(void) {
    RuntimeInitArgs args = {0};
    args.mem_alloc_type = Alloc_With_System_Allocator;
    PROBE_REQUIRE(wasm_runtime_full_init(&args));
    uint8_t bytes[sizeof(guest_minimal_bytes)];
    memcpy(bytes, guest_minimal_bytes, sizeof(bytes));
    char error[128] = {0};
    wasm_module_t module = wasm_runtime_load(bytes, sizeof(bytes), error, sizeof(error));
    PROBE_REQUIRE(module != NULL);
    wasm_module_inst_t instance = wasm_runtime_instantiate(module, 4096, 0, error, sizeof(error));
    PROBE_REQUIRE(instance != NULL);
    wasm_exec_env_t execution = wasm_runtime_create_exec_env(instance, 4096);
    PROBE_REQUIRE(execution != NULL);
    wasm_function_inst_t step = wasm_runtime_lookup_function(instance, "step");
    PROBE_REQUIRE(step != NULL);
    uint32_t result = 0;
    PROBE_REQUIRE(wasm_runtime_call_wasm(execution, step, 0, &result) && result == 7);
    PROBE_REQUIRE(wasm_runtime_detect_native_stack_overflow(execution));
    PROBE_REQUIRE(wasm_runtime_detect_native_stack_overflow_size(execution, 1024));
    // Synthetic shortage must still trap without actually overflowing the host stack.
    uint8_t* shortage = (uint8_t*)((uintptr_t)__builtin_frame_address(0) + 65536);
    wasm_runtime_set_native_stack_boundary(execution, shortage);
    PROBE_REQUIRE(!wasm_runtime_call_wasm(execution, step, 0, &result));
    PROBE_REQUIRE(strstr(wasm_runtime_get_exception(instance), "native stack overflow") != NULL);
    wasm_runtime_clear_exception(instance);
    PROBE_REQUIRE(!wasm_runtime_detect_native_stack_overflow_size(execution, 1024));
    wasm_runtime_clear_exception(instance);
    wasm_runtime_set_native_stack_boundary(execution, NULL);
    PROBE_REQUIRE(wasm_runtime_call_wasm(execution, step, 0, &result) && result == 7);
    PROBE_REQUIRE(wasm_runtime_detect_native_stack_overflow_size(execution, 1024));
    wasm_runtime_destroy_exec_env(execution);
    wasm_runtime_deinstantiate(instance);
    wasm_runtime_unload(module);
    wasm_runtime_destroy();
}
#endif

static int32_t Deliver(void* context, const uint8_t* bytes, uint32_t size) {
    (void)context;
    int32_t ignored = 99;
    PROBE_REQUIRE(!note4_wasm_call("step", &ignored) && ignored == 99);
    note4_wasm_stop(); // Reentrant destruction must not free a running import.
    return probe_emit(bytes, size);
}
static Note4WasmOptions options = {10000, 128 * 1024, Deliver, NULL};
static void Closed(void) {
    note4_wasm_stop();
    PROBE_REQUIRE(note4_wasm_heap().live == 0);
}
int note4_runtime_qualify(ProbeMode mode) {
    int32_t result = 0;
#if defined(NOTE4_WASM_WAMR) && !defined(ESP_PLATFORM)
    if (mode == PROBE_NORMAL)
        NativeStackChecks();
#endif
    if (mode == PROBE_START || mode == PROBE_POST) {
        const uint8_t* bytes = mode == PROBE_START ? guest_start_bytes : guest_post_bytes;
        const size_t size =
            mode == PROBE_START ? sizeof(guest_start_bytes) : sizeof(guest_post_bytes);
        puts("{\"event\":\"init-start\"}");
        fflush(stdout);
        PROBE_REQUIRE(note4_wasm_admit(bytes, size) == NOTE4_WASM_AUTO_INIT);
        PROBE_REQUIRE(!note4_wasm_start(bytes, size, options));
        Closed();
        puts("{\"event\":\"init-stopped\"}");
        return 0;
    }
    // Callers release source immediately; neither engine may borrow it.
    uint8_t* input = malloc(sizeof(guest_bytes));
    PROBE_REQUIRE(input != NULL);
    memcpy(input, guest_bytes, sizeof(guest_bytes));
    PROBE_REQUIRE(note4_wasm_start(input, sizeof(guest_bytes), options));
    memset(input, 0, sizeof(guest_bytes));
    free(input);
    if (mode == PROBE_SPIN) {
        puts("{\"event\":\"spin-start\"}");
        fflush(stdout);
        result = 77;
        PROBE_REQUIRE(!note4_wasm_call("spin", &result) && result == 77);
        PROBE_REQUIRE(strstr(note4_wasm_error(), "instruction limit") != NULL);
        Closed();
        puts("{\"event\":\"spin-trapped\"}");
        return 0;
    }
    const int step_ok = note4_wasm_call("step", &result);
    if (!step_ok || result != 2080)
        fprintf(stderr, "First step: engine=%s success=%d result=%d error=%s\n",
                note4_wasm_engine(), step_ok, (int)result, note4_wasm_error());
    PROBE_REQUIRE(step_ok && result == 2080);
    // Code-page immediates must preserve 32/64-bit integer and float bits.
    PROBE_REQUIRE(note4_wasm_call("constant32", &result) && result == 0x12345678);
    PROBE_REQUIRE(note4_wasm_call("constant64", &result) && result == 1);
    PROBE_REQUIRE(note4_wasm_call("constant64_high", &result) && result == 0x12345678);
    PROBE_REQUIRE(note4_wasm_call("constant_float64", &result) && result == 1);
    PROBE_REQUIRE(note4_wasm_call("constant_float64_value", &result) && result == 10);
    const uint64_t start = probe_now_us();
    for (unsigned i = 0; i < 10000; ++i)
        PROBE_REQUIRE(note4_wasm_call("step", &result) && result == 2080);
    const uint64_t elapsed = probe_now_us() - start;
    PROBE_REQUIRE(probe_emissions == 10001);
    const unsigned emitted = probe_emissions;
    PROBE_REQUIRE(note4_wasm_call("bad_import", &result) && result == -1);
    PROBE_REQUIRE(note4_wasm_call("long_import", &result) && result == -1 &&
                  probe_emissions == emitted);
    PROBE_REQUIRE(note4_wasm_call("grow", &result) && result == -1);
    PROBE_REQUIRE(!note4_wasm_call("out_of_bounds", &result));
    PROBE_REQUIRE(strstr(note4_wasm_error(), "out of bounds") != NULL);
    PROBE_REQUIRE(!note4_wasm_call("recurse", &result));
    result = 77;
    PROBE_REQUIRE(!note4_wasm_call("wrong_result", &result) && result == 77);
    PROBE_REQUIRE(!note4_wasm_call("needs_argument", &result) && result == 77);
    PROBE_REQUIRE(!note4_wasm_call("absent", &result) && result == 77);
    const size_t normal_peak = note4_wasm_heap().peak;
    Closed();
    PROBE_REQUIRE(!note4_wasm_start(guest_bytes, 7, options));
    Closed();
    const uint8_t malformed[] = {0, 97, 115, 109, 1, 0, 0, 0, 1, 2, 1, 255};
    PROBE_REQUIRE(note4_wasm_admit(malformed, sizeof(malformed)) == NOTE4_WASM_ACCEPT);
    PROBE_REQUIRE(!note4_wasm_start(malformed, sizeof(malformed), options));
    Closed();
    PROBE_REQUIRE(note4_wasm_start(guest_minimal_bytes, sizeof(guest_minimal_bytes), options));
    PROBE_REQUIRE(note4_wasm_call("step", &result) && result == 7);
    Closed();
    for (unsigned i = 0; i < 100; ++i) {
        PROBE_REQUIRE(note4_wasm_start(guest_bytes, sizeof(guest_bytes), options));
        PROBE_REQUIRE(note4_wasm_call("step", &result) && result == 2080);
        Closed();
    }
    options.heap_bytes = 32 * 1024;
    PROBE_REQUIRE(!note4_wasm_start(guest_bytes, sizeof(guest_bytes), options));
    Closed();
    PROBE_REQUIRE(note4_wasm_heap().rejected > 0);
    printf("{\"engine\":\"%s\",\"normal_peak_bytes\":%zu,\"live_after_close\":%zu,"
           "\"allocation_rejections\":%zu,\"step_10000_us\":%llu,\"cycles\":100}\n",
           note4_wasm_engine(), normal_peak, note4_wasm_heap().live, note4_wasm_heap().rejected,
           (unsigned long long)elapsed);
    return 0;
}
