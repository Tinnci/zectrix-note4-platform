include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/zectrix_features.cmake")
set(sources "zectrix_storage_service.cc")
set(private_requires)
if(CONFIG_ZECTRIX_ENABLE_BOOK_STORAGE)
    list(APPEND sources "zectrix_book_storage.cc")
    list(APPEND private_requires spiffs)
endif()
idf_component_register(
    SRCS ${sources}
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash
    PRIV_REQUIRES ${private_requires}
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
