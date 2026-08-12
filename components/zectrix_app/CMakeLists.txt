idf_component_register(
    SRCS "zectrix_app_contract.cc" "zectrix_application_runtime.cc"
         "zectrix_first_party_app_controllers.cc"
    INCLUDE_DIRS "include"
    REQUIRES zectrix_input
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
