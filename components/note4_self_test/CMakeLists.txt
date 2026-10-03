include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/note4_features.cmake")
set(private_requires)
if(CONFIG_NOTE4_ENABLE_WIFI)
    list(APPEND private_requires note4_connectivity)
endif()
idf_component_register(
    SRCS "note4_self_test.cc" "acoustic_selftest.cc"
    INCLUDE_DIRS "." "include"
    REQUIRES
        note4_board
        note4_input
        note4_power
        note4_storage
        note4_system
        note4_time
    PRIV_REQUIRES ${private_requires}
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
