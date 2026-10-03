idf_component_register(
    SRCS "note4_host_channel.cc" "note4_host_protocol.cc" "note4_host_books.cc"
    INCLUDE_DIRS "include"
    REQUIRES note4_cli note4_storage
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
