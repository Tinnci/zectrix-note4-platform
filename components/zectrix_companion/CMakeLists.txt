idf_component_register(
    SRCS "zectrix_companion_protocol.cc"
    INCLUDE_DIRS "include"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
