idf_component_register(
    SRCS "note4_power_service.cc"
    INCLUDE_DIRS "include"
    REQUIRES note4_board
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
