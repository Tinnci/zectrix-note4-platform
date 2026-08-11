idf_component_register(
    SRCS "zectrix_time_service.cc"
    INCLUDE_DIRS "include"
    REQUIRES zectrix_board esp_timer
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
