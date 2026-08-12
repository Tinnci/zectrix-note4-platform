idf_component_register(
    SRCS "zectrix_input_service.cc"
    INCLUDE_DIRS "include"
    REQUIRES zectrix_app zectrix_board
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
