idf_component_register(
    SRCS "zectrix_app_contract.cc"
    INCLUDE_DIRS "include"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
