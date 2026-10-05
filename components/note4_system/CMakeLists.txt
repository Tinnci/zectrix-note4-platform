set(watchdog_requires hal)
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    list(APPEND watchdog_requires esp_hal_wdt)
endif()
idf_component_register(
    SRCS "note4_system_service.cc" "note4_boot_guard.cc" "note4_boot_esp.cc"
         "note4_health_supervisor.cc" "note4_health_esp.cc"
    INCLUDE_DIRS "include"
    REQUIRES note4_board esp_app_format esp_hw_support heap spi_flash
    PRIV_REQUIRES app_update esp_partition esp_timer ${watchdog_requires}
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
