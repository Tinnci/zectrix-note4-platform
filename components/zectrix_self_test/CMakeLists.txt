idf_component_register(
    SRCS "zectrix_self_test.cc" "acoustic_selftest.cc"
    INCLUDE_DIRS "." "include"
    REQUIRES
        zectrix_board
        zectrix_storage
        zectrix_time
        esp_event
        esp_netif
        esp_wifi
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
