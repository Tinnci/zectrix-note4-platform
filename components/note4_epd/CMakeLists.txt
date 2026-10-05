idf_component_register(
    SRCS "note4_epd.cc" "note4_epd_calibration.cc"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES note4_log esp_driver_gpio esp_driver_spi esp_timer
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
