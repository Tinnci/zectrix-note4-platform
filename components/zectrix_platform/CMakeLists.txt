idf_component_register(
    SRCS "zectrix_platform.cc" "zectrix_platform_diagnostics.cc"
         "zectrix_service_registry.cc"
    INCLUDE_DIRS "include"
    REQUIRES
        zectrix_board
        zectrix_cli
        zectrix_connectivity
        zectrix_display
        zectrix_input
        zectrix_nfc_service
        zectrix_power
        zectrix_self_test
        zectrix_storage
        zectrix_system
        zectrix_time
        zectrix_update
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
