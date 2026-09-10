#include "probe.h"

#include "lua.h"
#include "lauxlib.h"

#include <stdio.h>

static const char script[] =
    "function step() local sum = 0; for i = 1,64 do sum = sum + i end; "
    "emit('NOTE4'); return sum end\n"
    "function spin() while true do end end\n"
    "function exhaust() local t = {}; for i = 1,10000000 do t[i] = i end end\n"
    "function bad_import() return emit('"
    "01234567890123456789012345678901234567890123456789012345678901234567890') end\n";

static lua_State* owner;
static lua_State* callback;

static void* allocate(void* context, void* pointer, size_t old_size, size_t size) {
    (void)context;
    (void)old_size;
    return probe_realloc(pointer, size);
}

static int emit(lua_State* state) {
    size_t size = 0;
    const char* bytes = luaL_checklstring(state, 1, &size);
    lua_pushinteger(state, size > 64 ? -1 : probe_emit(bytes, (uint32_t)size));
    return 1;
}

static void budget(lua_State* state, lua_Debug* debug) {
    (void)debug;
    // Yield to the C owner; a guest pcall cannot swallow this as an error.
    lua_yield(state, 0);
}

static void open_guest(void) {
    owner = lua_newstate(allocate, NULL);
    PROBE_REQUIRE(owner != NULL);
    // No io, os, package, debug, coroutine or dynamically loaded C libraries.
    lua_pushcfunction(owner, emit);
    lua_setglobal(owner, "emit");
    PROBE_REQUIRE(luaL_loadbufferx(owner, script, sizeof(script) - 1, "probe", "t") == LUA_OK);
    PROBE_REQUIRE(lua_pcall(owner, 0, 0, 0) == LUA_OK);
    callback = lua_newthread(owner);
    PROBE_REQUIRE(callback != NULL);
}

static int call_guest(const char* name, int metered) {
    PROBE_REQUIRE(lua_resetthread(callback) == LUA_OK);
    lua_sethook(callback, metered ? budget : NULL, LUA_MASKCOUNT, 10000);
    lua_getglobal(callback, name);
    int results = 0;
    return lua_resume(callback, owner, 0, &results);
}

static void close_guest(void) {
    lua_close(owner);
    owner = NULL;
    callback = NULL;
    PROBE_REQUIRE(probe_heap.live == 0);
}

int research_probe(int spin_only) {
    probe_heap.limit = 64 * 1024;
    open_guest();
    if (spin_only) {
        puts("{\"event\":\"spin-start\"}");
        fflush(stdout);
        PROBE_REQUIRE(call_guest("spin", 1) == LUA_YIELD);
        close_guest();
        puts("{\"event\":\"spin-trapped\"}");
        return 0;
    }
    const uint64_t start = probe_now_us();
    for (unsigned i = 0; i < 10000; ++i) {
        PROBE_REQUIRE(call_guest("step", 1) == LUA_OK);
        PROBE_REQUIRE(lua_tointeger(callback, -1) == 2080);
    }
    const uint64_t elapsed = probe_now_us() - start;
    PROBE_REQUIRE(probe_emissions == 10000);
    PROBE_REQUIRE(call_guest("bad_import", 1) == LUA_OK);
    PROBE_REQUIRE(lua_tointeger(callback, -1) == -1 && probe_emissions == 10000);
    close_guest();
    for (unsigned i = 0; i < 100; ++i) {
        open_guest();
        PROBE_REQUIRE(call_guest("step", 1) == LUA_OK);
        close_guest();
    }
    const size_t normal_peak = probe_heap.peak;
    open_guest();
    PROBE_REQUIRE(call_guest("exhaust", 0) == LUA_ERRMEM);
    close_guest();
    PROBE_REQUIRE(probe_heap.rejected > 0);
    printf("{\"engine\":\"lua\",\"normal_peak_bytes\":%zu,\"oom_peak_bytes\":%zu,"
           "\"live_after_close\":%zu,\"allocation_rejections\":%zu,"
           "\"step_10000_us\":%llu,\"cycles\":100}\n",
           normal_peak, probe_heap.peak, probe_heap.live, probe_heap.rejected,
           (unsigned long long)elapsed);
    return 0;
}
