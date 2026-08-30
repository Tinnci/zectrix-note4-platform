idf_component_register(
    SRCS
        "zectrix_companion_identity.cc"
        "zectrix_companion_protocol.cc"
        "zectrix_connectivity_policy.cc"
        "zectrix_enrollment_ndef.cc"
        "zectrix_pairing_bootstrap.cc"
        "zectrix_resource_gateway.cc"
        "zectrix_sync_engine.cc"
    INCLUDE_DIRS "include"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
