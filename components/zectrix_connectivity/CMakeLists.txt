idf_component_register(
    SRCS
        "zectrix_ble_link.cc"
        "zectrix_connectivity_service.cc"
    INCLUDE_DIRS "include"
    REQUIRES bt esp_system zectrix_companion
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
