# Shared production/qualification build for one bounded Wasm interpreter.
function(note4_add_wasm_engine target engine source)
    if(engine STREQUAL "wasm3")
        set(root "${source}/source")
        set(files m3_bind.c m3_code.c m3_compile.c m3_core.c m3_emit.c
            m3_env.c m3_exec.c m3_function.c m3_info.c m3_module.c m3_parse.c)
        list(TRANSFORM files PREPEND "${root}/")
        add_library(${target} STATIC ${files})
        target_include_directories(${target} PUBLIC "${root}")
        target_compile_definitions(${target} PRIVATE
            malloc=note4_wasm_malloc calloc=note4_wasm_calloc realloc=note4_wasm_realloc free=note4_wasm_free
            d_m3MaxFunctionStackHeight=128 d_m3MaxLinearMemoryPages=1 d_m3VerboseErrorMessages=0)
        target_compile_options(${target} PRIVATE -include "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/include/note4_wasm.h")
    elseif(engine STREQUAL "wamr")
        set(WAMR_ROOT_DIR "${source}")
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
        set(WAMR_DISABLE_HW_BOUND_CHECK 1)
        include("${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake")
        if(ESP_PLATFORM)
            # The upstream ESP-IDF port globs WASI file/socket adapters even
            # when WASI is disabled. Neither is part of Note4's guest surface;
            # avoid compiling their unused POSIX dependencies (not portable
            # to IDF 6's default libc). Keep clocks, allocation and threading.
            list(FILTER WAMR_RUNTIME_LIB_SOURCE EXCLUDE
                REGEX "/esp-idf/espidf_(file|socket)\\.c$")
        endif()
        add_library(${target} STATIC ${WAMR_RUNTIME_LIB_SOURCE})
        target_include_directories(${target} PUBLIC "${WAMR_ROOT_DIR}/core/iwasm/include")
        target_compile_definitions(${target} PUBLIC NOTE4_WASM_WAMR=1)
    else()
        message(FATAL_ERROR "Choose wamr or wasm3")
    endif()
    target_compile_options(${target} PRIVATE -Os -ffunction-sections -fdata-sections)
    target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/include")
    if(ESP_PLATFORM)
        target_link_libraries(${target} PRIVATE idf::esp_timer idf::pthread idf::esp_psram)
    else()
        target_link_libraries(${target} PUBLIC m pthread)
    endif()
endfunction()
