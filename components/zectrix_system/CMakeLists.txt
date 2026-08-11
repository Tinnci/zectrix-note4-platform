idf_component_register(
    SRCS "zectrix_system_service.cc"
    INCLUDE_DIRS "include"
    REQUIRES zectrix_board esp_app_format esp_hw_support heap spi_flash
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
