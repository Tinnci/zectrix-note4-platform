set(COMPONENT_KCONFIG_PROJBUILD "${CMAKE_CURRENT_LIST_DIR}/../../cmake/Kconfig.empty")
idf_component_register(SRCS "zectrix_runtime.cc" INCLUDE_DIRS "include"
    REQUIRES zectrix_text PRIV_REQUIRES heap esp_psram)
if(CMAKE_BUILD_EARLY_EXPANSION)
    return()
endif()
include(lua.cmake)
target_link_libraries(${COMPONENT_LIB} PRIVATE note4_lua)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
