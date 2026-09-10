if(RESEARCH_ENGINE STREQUAL "lua")
    set(probe_source "../probe_lua.c")
else()
    set(probe_source "../probe_wasm.c")
endif()
idf_component_register(SRCS "../probe.c" "${probe_source}"
    INCLUDE_DIRS ".." "${RESEARCH_GENERATED_DIR}"
    PRIV_REQUIRES esp_timer esp_psram pthread)
if(CMAKE_BUILD_EARLY_EXPANSION)
    return()
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../engine.cmake")
target_link_libraries(${COMPONENT_LIB} PRIVATE research_engine)
# Retain the probe for a real Full-image link measurement, without executing it.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u research_probe")
