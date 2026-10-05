idf_component_register(
    SRCS
        "note4_cli_core.cc"
        "note4_cli_session.cc"
        "note4_cli_usb.cc"
        "note4_cli_control.cc"
        "note4_cli_diagnostics.cc"
        "note4_cli_log.cc"
        "note4_cli_log_esp.cc"
    INCLUDE_DIRS "include"
    REQUIRES note4_log esp_driver_usb_serial_jtag note4_display note4_epd note4_system note4_board note4_time
    KCONFIG_PROJBUILD "${CMAKE_CURRENT_LIST_DIR}/../../cmake/Kconfig.empty"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
