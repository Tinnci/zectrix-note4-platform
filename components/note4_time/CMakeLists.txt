idf_component_register(
    SRCS "note4_time_service.cc" "note4_time_sync.cc"
    INCLUDE_DIRS "include"
    REQUIRES note4_board esp_timer
    PRIV_REQUIRES note4_storage
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
