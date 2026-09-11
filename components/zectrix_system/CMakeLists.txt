idf_component_register(
    SRCS "zectrix_system_service.cc" "zectrix_boot_guard.cc" "zectrix_boot_esp.cc"
         "zectrix_health_supervisor.cc" "zectrix_health_esp.cc"
    INCLUDE_DIRS "include"
    REQUIRES zectrix_board esp_app_format esp_hw_support heap spi_flash
    PRIV_REQUIRES app_update esp_partition esp_timer hal
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
