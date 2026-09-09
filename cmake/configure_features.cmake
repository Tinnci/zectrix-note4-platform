# Use IDF's own Kconfig library, with IDF's defaults -> target defaults ->
# sdkconfig precedence. This also works for a fresh, separate build directory.
set(zectrix_defaults "$ENV{SDKCONFIG_DEFAULTS}")
if(NOT zectrix_defaults AND EXISTS "${CMAKE_SOURCE_DIR}/sdkconfig.defaults")
    set(zectrix_defaults "${CMAKE_SOURCE_DIR}/sdkconfig.defaults")
endif()
if(SDKCONFIG_DEFAULTS)
    set(zectrix_defaults "${SDKCONFIG_DEFAULTS}")
endif()
set(zectrix_config "${CMAKE_SOURCE_DIR}/sdkconfig")
if(SDKCONFIG)
    get_filename_component(zectrix_config "${SDKCONFIG}" ABSOLUTE)
endif()
set(zectrix_config_inputs "${zectrix_config}")
set(zectrix_default_args)
foreach(config_default IN LISTS zectrix_defaults)
    get_filename_component(config_default "${config_default}" ABSOLUTE)
    list(APPEND zectrix_default_args --defaults "${config_default}")
    list(APPEND zectrix_config_inputs "${config_default}")
    if(EXISTS "${config_default}.${IDF_TARGET}")
        list(APPEND zectrix_default_args --defaults "${config_default}.${IDF_TARGET}")
        list(APPEND zectrix_config_inputs "${config_default}.${IDF_TARGET}")
    endif()
endforeach()

idf_build_get_property(zectrix_python PYTHON)
set(zectrix_features_file "${CMAKE_BINARY_DIR}/zectrix_features.cmake")
execute_process(
    COMMAND "${zectrix_python}" "${CMAKE_CURRENT_LIST_DIR}/configure_features.py"
        --kconfig "${CMAKE_SOURCE_DIR}/main/Kconfig.projbuild"
        --config "${zectrix_config}" ${zectrix_default_args}
        --output "${zectrix_features_file}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE zectrix_config_result
)
if(NOT zectrix_config_result EQUAL 0)
    message(FATAL_ERROR "Could not resolve Zectrix module Kconfig options")
endif()
idf_build_set_property(ZECTRIX_FEATURES_FILE "${zectrix_features_file}")
file(GLOB zectrix_module_kconfigs "${CMAKE_SOURCE_DIR}/components/zectrix_*/Kconfig.projbuild")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${zectrix_config_inputs} ${zectrix_module_kconfigs}
    "${CMAKE_SOURCE_DIR}/main/Kconfig.projbuild"
    "${CMAKE_CURRENT_LIST_DIR}/configure_features.py")
