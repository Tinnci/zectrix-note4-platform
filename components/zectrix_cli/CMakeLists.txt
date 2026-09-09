idf_component_register(
    SRCS
        "zectrix_cli_core.cc"
        "zectrix_cli_session.cc"
        "zectrix_cli_usb.cc"
        "zectrix_cli_control.cc"
        "zectrix_cli_diagnostics.cc"
        "zectrix_cli_log.cc"
        "zectrix_cli_log_esp.cc"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_usb_serial_jtag zectrix_display zectrix_system
    KCONFIG_PROJBUILD "${CMAKE_CURRENT_LIST_DIR}/../../cmake/Kconfig.empty"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
