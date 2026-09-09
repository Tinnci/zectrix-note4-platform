idf_component_register(
    SRCS
        "zectrix_ble_link.cc"
        "zectrix_connectivity_service.cc"
        "zectrix_resource_client.cc"
        "zectrix_wifi_backend.cc"
        "zectrix_wifi_credentials.cc"
        "zectrix_wifi_esp_driver.cc"
        "zectrix_wifi_http.cc"
        "zectrix_wifi_http_response.cc"
        "zectrix_book_transfer.cc"
        "zectrix_book_web.cc"
        "zectrix_book_web_esp.cc"
        "zectrix_book_web_data.S"
    INCLUDE_DIRS "include"
    REQUIRES
        bt
        esp_system
        esp_timer
        zectrix_companion
        zectrix_nfc_service
        zectrix_storage
    PRIV_REQUIRES esp_event esp_netif esp_wifi esp-tls mbedtls lwip
                  esp_http_client esp_http_server tcp_transport zectrix_power
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ZECTRIX_BOOK_WEB_PATH="${CMAKE_CURRENT_SOURCE_DIR}/web/books.html")
set_source_files_properties(zectrix_book_web_data.S PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/web/books.html")
