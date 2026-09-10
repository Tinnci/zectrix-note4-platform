# Keep upstream engines outside the firmware source tree and normal Host suite.
set(probe_dir "${CMAKE_CURRENT_LIST_DIR}")
if(RESEARCH_ENGINE STREQUAL "wasm3")
    set(engine_root "${RESEARCH_SOURCES}/wasm3/source")
    set(engine_sources m3_bind.c m3_code.c m3_compile.c m3_core.c m3_emit.c
        m3_env.c m3_exec.c m3_function.c m3_info.c m3_module.c m3_parse.c)
    list(TRANSFORM engine_sources PREPEND "${engine_root}/")
    add_library(research_engine STATIC ${engine_sources})
    target_include_directories(research_engine PUBLIC "${engine_root}")
    target_compile_definitions(research_engine PRIVATE
        malloc=probe_malloc calloc=probe_calloc realloc=probe_realloc free=probe_free
        d_m3MaxFunctionStackHeight=128 d_m3MaxLinearMemoryPages=1
        d_m3VerboseErrorMessages=0)
    target_compile_options(research_engine PRIVATE -include "${probe_dir}/probe.h")
elseif(RESEARCH_ENGINE STREQUAL "wamr")
    set(WAMR_ROOT_DIR "${RESEARCH_SOURCES}/wamr")
    if(ESP_PLATFORM)
        set(WAMR_BUILD_PLATFORM "esp-idf")
        set(WAMR_BUILD_TARGET "XTENSA")
    elseif(APPLE)
        set(WAMR_BUILD_PLATFORM "darwin")
    else()
        set(WAMR_BUILD_PLATFORM "linux")
    endif()
    set(WAMR_BUILD_INTERP 1)
    set(WAMR_BUILD_INSTRUCTION_METERING 1)
    set(WAMR_BUILD_ALLOC_WITH_USAGE 1)
    foreach(feature AOT JIT FAST_JIT FAST_INTERP LIBC_BUILTIN LIBC_WASI
            SIMD REF_TYPES BULK_MEMORY MULTI_MODULE LIB_PTHREAD SHARED_MEMORY
            THREAD_MGR GC MEMORY64 MINI_LOADER SHRUNK_MEMORY)
        set(WAMR_BUILD_${feature} 0)
    endforeach()
    # Exercise the same software bounds checks on the Host and ESP32-S3.
    set(WAMR_DISABLE_HW_BOUND_CHECK 1)
    include("${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake")
    add_library(research_engine STATIC ${WAMR_RUNTIME_LIB_SOURCE})
    target_include_directories(research_engine PUBLIC "${WAMR_ROOT_DIR}/core/iwasm/include")
    target_compile_definitions(research_engine PUBLIC PROBE_WAMR=1)
elseif(RESEARCH_ENGINE STREQUAL "lua")
    set(engine_root "${RESEARCH_SOURCES}/lua")
    set(engine_sources lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c
        lgc.c llex.c lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c
        ltable.c ltm.c lundump.c lvm.c lzio.c lauxlib.c)
    list(TRANSFORM engine_sources PREPEND "${engine_root}/")
    add_library(research_engine STATIC ${engine_sources})
    target_include_directories(research_engine PUBLIC "${engine_root}")
else()
    message(FATAL_ERROR "Select RESEARCH_ENGINE=wasm3, wamr or lua")
endif()

target_compile_options(research_engine PRIVATE -Os -ffunction-sections -fdata-sections)
if(ESP_PLATFORM)
    # The upstream ESP-IDF port uses pthread and timer declarations internally.
    target_link_libraries(research_engine PRIVATE idf::esp_timer idf::pthread idf::esp_psram)
else()
    target_link_libraries(research_engine PUBLIC m pthread)
endif()

if(RESEARCH_ENGINE STREQUAL "lua")
    set(probe_sources "${probe_dir}/probe.c" "${probe_dir}/probe_lua.c")
else()
    set(probe_sources "${probe_dir}/probe.c" "${probe_dir}/probe_wasm.c")
endif()
