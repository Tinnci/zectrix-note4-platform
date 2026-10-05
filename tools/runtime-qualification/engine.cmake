# Keep upstream engines outside the firmware source tree and normal Host suite.
set(probe_dir "${CMAKE_CURRENT_LIST_DIR}")
if(NOT QUALIFY_ENGINE STREQUAL "lua")
    include("${probe_dir}/../../components/note4_runtime/wasm.cmake")
    note4_add_wasm_engine(qualification_engine ${QUALIFY_ENGINE} "${QUALIFY_SOURCES}/${QUALIFY_ENGINE}")
else()
    set(engine_root "${QUALIFY_SOURCES}/lua")
    set(engine_sources lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c
        lgc.c llex.c lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c
        ltable.c ltm.c lundump.c lvm.c lzio.c lauxlib.c)
    list(TRANSFORM engine_sources PREPEND "${engine_root}/")
    add_library(qualification_engine STATIC ${engine_sources})
    target_include_directories(qualification_engine PUBLIC "${engine_root}")
endif()

target_compile_options(qualification_engine PRIVATE -Os -ffunction-sections -fdata-sections)
if(ESP_PLATFORM)
    # The upstream ESP-IDF port uses pthread and timer declarations internally.
    target_link_libraries(qualification_engine PRIVATE idf::esp_timer idf::pthread idf::esp_psram)
else()
    target_link_libraries(qualification_engine PUBLIC m pthread)
endif()

if(QUALIFY_ENGINE STREQUAL "lua")
    set(probe_sources "${probe_dir}/probe.c" "${probe_dir}/probe_lua.c")
else()
    set(probe_sources "${probe_dir}/probe.c" "${probe_dir}/probe_wasm.c"
        "${probe_dir}/../../components/note4_runtime/note4_wasm.c"
        "${probe_dir}/../../components/note4_runtime/note4_wasm_admission.c")
endif()
target_include_directories(qualification_engine PUBLIC "${probe_dir}/../../components/note4_runtime/include")
