idf_component_register(
    SRCS
        "zectrix_ble_link.cc"
        "zectrix_connectivity_service.cc"
    INCLUDE_DIRS "include"
    REQUIRES
        bt
        esp_system
        esp_timer
        zectrix_companion
        zectrix_nfc_service
        zectrix_storage
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
