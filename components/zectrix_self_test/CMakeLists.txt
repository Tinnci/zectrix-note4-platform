include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/zectrix_features.cmake")
set(private_requires)
if(CONFIG_ZECTRIX_ENABLE_WIFI)
    list(APPEND private_requires zectrix_connectivity)
endif()
idf_component_register(
    SRCS "zectrix_self_test.cc" "acoustic_selftest.cc"
    INCLUDE_DIRS "." "include"
    REQUIRES
        zectrix_board
        zectrix_input
        zectrix_power
        zectrix_storage
        zectrix_system
        zectrix_time
    PRIV_REQUIRES ${private_requires}
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
