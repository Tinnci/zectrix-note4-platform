idf_component_register(
    SRCS
        "note4_companion_identity.cc"
        "note4_companion_protocol.cc"
        "note4_connectivity_policy.cc"
        "note4_enrollment_ndef.cc"
        "note4_enrollment_publisher.cc"
        "note4_pairing_bootstrap.cc"
        "note4_resource_gateway.cc"
        "note4_sync_engine.cc"
        "note4_sync_session.cc"
    INCLUDE_DIRS "include"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
