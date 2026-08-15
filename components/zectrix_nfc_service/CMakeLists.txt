idf_component_register(
    SRCS "zectrix_nfc_service.cc"
    INCLUDE_DIRS "include"
    REQUIRES zectrix_board zectrix_companion
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
