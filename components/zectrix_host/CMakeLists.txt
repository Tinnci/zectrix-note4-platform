idf_component_register(
    SRCS "zectrix_host_channel.cc" "zectrix_host_protocol.cc" "zectrix_host_books.cc"
    INCLUDE_DIRS "include"
    REQUIRES zectrix_cli zectrix_storage
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
