idf_component_register(
    SRCS "zectrix_platform.cc"
    INCLUDE_DIRS "include"
    REQUIRES
        zectrix_board
        zectrix_display
        zectrix_input
        zectrix_power
        zectrix_self_test
        zectrix_storage
        zectrix_system
        zectrix_time
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
