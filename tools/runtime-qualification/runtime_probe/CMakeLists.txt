if(QUALIFY_ENGINE STREQUAL "lua")
    set(probe_source "../probe_lua.c")
else()
    set(probe_source "../probe_wasm.c")
endif()
idf_component_register(SRCS "../probe.c" "${probe_source}"
    INCLUDE_DIRS ".." "${QUALIFY_GENERATED_DIR}" "../../../components/note4_runtime/include"
    PRIV_REQUIRES esp_timer esp_psram pthread note4_runtime)
if(CMAKE_BUILD_EARLY_EXPANSION)
    return()
endif()
if(QUALIFY_ENGINE STREQUAL "lua")
    include("${CMAKE_CURRENT_LIST_DIR}/../engine.cmake")
    target_link_libraries(${COMPONENT_LIB} PRIVATE qualification_engine)
endif()
# Retain the probe for a real Full-image link measurement, without executing it.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u note4_runtime_qualify")
