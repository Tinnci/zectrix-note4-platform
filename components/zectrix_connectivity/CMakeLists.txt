idf_component_register(
    SRCS
        "zectrix_ble_link.cc"
        "zectrix_connectivity_service.cc"
        "zectrix_wifi_backend.cc"
        "zectrix_wifi_credentials.cc"
        "zectrix_wifi_esp_driver.cc"
        "zectrix_wifi_http.cc"
    INCLUDE_DIRS "include"
    REQUIRES
        bt
        esp_system
        esp_timer
        zectrix_companion
        zectrix_nfc_service
        zectrix_storage
    PRIV_REQUIRES esp_event esp_netif esp_wifi esp-tls mbedtls lwip
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
