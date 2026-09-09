idf_component_register(
    SRCS "zectrix_update_stream.cc" "zectrix_update_esp.cc"
    INCLUDE_DIRS "include"
    REQUIRES zectrix_system
    PRIV_REQUIRES app_update bootloader_support esp_app_format esp_partition
    KCONFIG_PROJBUILD "${CMAKE_CURRENT_LIST_DIR}/../../cmake/Kconfig.empty"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
