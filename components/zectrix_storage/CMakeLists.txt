idf_component_register(
    SRCS "zectrix_storage_service.cc" "zectrix_book_storage.cc"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash
    PRIV_REQUIRES spiffs
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
