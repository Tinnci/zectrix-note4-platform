idf_component_register(
    SRCS "zectrix_time_service.cc" "zectrix_time_sync.cc"
    INCLUDE_DIRS "include"
    REQUIRES zectrix_board esp_timer
    PRIV_REQUIRES zectrix_storage
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
