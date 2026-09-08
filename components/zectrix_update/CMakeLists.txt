idf_component_register(
    SRCS "zectrix_update_service.cc" "zectrix_update_esp.cc"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES app_update esp_partition spi_flash esp_timer esp_hw_support hal
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
