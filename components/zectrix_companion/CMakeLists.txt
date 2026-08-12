idf_component_register(
    SRCS
        "zectrix_companion_protocol.cc"
        "zectrix_connectivity_policy.cc"
        "zectrix_sync_engine.cc"
    INCLUDE_DIRS "include"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
