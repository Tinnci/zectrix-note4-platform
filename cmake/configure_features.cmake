# Use IDF's own Kconfig library, with IDF's defaults -> target defaults ->
# sdkconfig precedence. This also works for a fresh, separate build directory.
set(note4_defaults "$ENV{SDKCONFIG_DEFAULTS}")
if(NOT note4_defaults AND EXISTS "${CMAKE_SOURCE_DIR}/sdkconfig.defaults")
    set(note4_defaults "${CMAKE_SOURCE_DIR}/sdkconfig.defaults")
endif()
if(SDKCONFIG_DEFAULTS)
    set(note4_defaults "${SDKCONFIG_DEFAULTS}")
endif()
set(note4_config "${CMAKE_SOURCE_DIR}/sdkconfig")
if(SDKCONFIG)
    get_filename_component(note4_config "${SDKCONFIG}" ABSOLUTE)
endif()
set(note4_config_inputs "${note4_config}")
set(note4_default_args)
foreach(config_default IN LISTS note4_defaults)
    get_filename_component(config_default "${config_default}" ABSOLUTE)
    list(APPEND note4_default_args --defaults "${config_default}")
    list(APPEND note4_config_inputs "${config_default}")
    if(EXISTS "${config_default}.${IDF_TARGET}")
        list(APPEND note4_default_args --defaults "${config_default}.${IDF_TARGET}")
        list(APPEND note4_config_inputs "${config_default}.${IDF_TARGET}")
    endif()
endforeach()

idf_build_get_property(note4_python PYTHON)
set(note4_features_file "${CMAKE_BINARY_DIR}/note4_features.cmake")
execute_process(
    COMMAND "${note4_python}" "${CMAKE_CURRENT_LIST_DIR}/configure_features.py"
        --kconfig "${CMAKE_SOURCE_DIR}/main/Kconfig.projbuild"
        --config "${note4_config}" ${note4_default_args}
        --output "${note4_features_file}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE note4_config_result
)
if(NOT note4_config_result EQUAL 0)
    message(FATAL_ERROR "Could not resolve Note4 module Kconfig options")
endif()
idf_build_set_property(NOTE4_FEATURES_FILE "${note4_features_file}")
file(GLOB note4_module_kconfigs "${CMAKE_SOURCE_DIR}/components/note4_*/Kconfig.projbuild")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${note4_config_inputs} ${note4_module_kconfigs}
    "${CMAKE_SOURCE_DIR}/main/Kconfig.projbuild"
    "${CMAKE_CURRENT_LIST_DIR}/configure_features.py")
