include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/zectrix_features.cmake")
set(sources
        "zectrix_ble_link.cc"
        "zectrix_connectivity_service.cc"
        "zectrix_radio_arbiter.cc"
        "zectrix_resource_client.cc"
        "zectrix_wifi_backend.cc"
)
set(private_requires zectrix_power)
if(CONFIG_ZECTRIX_ENABLE_WIFI)
    list(APPEND sources "zectrix_wifi_credentials.cc" "zectrix_wifi_esp_driver.cc")
    list(APPEND private_requires esp_event esp_netif esp_wifi lwip)
endif()
if(CONFIG_ZECTRIX_ENABLE_WIFI_HTTP)
    list(APPEND sources "zectrix_wifi_http.cc" "zectrix_wifi_http_response.cc")
    list(APPEND private_requires esp-tls mbedtls esp_http_client tcp_transport)
endif()
if(CONFIG_ZECTRIX_ENABLE_BOOK_TRANSFER)
    list(APPEND sources
        "zectrix_book_transfer.cc"
        "zectrix_book_web.cc"
        "zectrix_book_web_esp.cc"
        "zectrix_book_web_data.S")
    list(APPEND private_requires esp_http_server)
endif()
idf_component_register(
    SRCS ${sources}
    INCLUDE_DIRS "include"
    REQUIRES
        bt
        esp_system
        esp_timer
        zectrix_companion
        zectrix_nfc_service
        zectrix_storage
    PRIV_REQUIRES ${private_requires}
    KCONFIG_PROJBUILD "${CMAKE_CURRENT_LIST_DIR}/../../cmake/Kconfig.empty"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
if(CONFIG_ZECTRIX_ENABLE_BOOK_TRANSFER)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        ZECTRIX_BOOK_WEB_PATH="${CMAKE_CURRENT_SOURCE_DIR}/web/books.html")
    set_source_files_properties(zectrix_book_web_data.S PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/web/books.html")
endif()
