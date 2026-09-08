idf_component_register(
    SRCS
        "zectrix_cli_core.cc"
        "zectrix_cli_session.cc"
        "zectrix_cli_usb.cc"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_usb_serial_jtag
)
