include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/note4_features.cmake")
set(sources
        "note4_ble_link.cc"
        "note4_connectivity_service.cc"
        "note4_connectivity_settings.cc"
        "note4_edge_settings.cc"
        "note4_radio_arbiter.cc"
        "note4_resource_client.cc"
        "note4_wifi_backend.cc"
)
set(private_requires note4_power)
if(CONFIG_NOTE4_ENABLE_WIFI)
    list(APPEND sources "note4_wifi_credentials.cc" "note4_wifi_esp_driver.cc")
    list(APPEND private_requires esp_event esp_netif esp_wifi lwip)
endif()
if(CONFIG_NOTE4_ENABLE_WIFI_HTTP)
    list(APPEND sources "note4_wifi_http.cc" "note4_wifi_http_response.cc" "note4_edge_sync.cc" "note4_edge_download.cc")
    list(APPEND private_requires esp-tls mbedtls esp_http_client tcp_transport)
endif()
if(CONFIG_NOTE4_ENABLE_BOOK_TRANSFER)
    list(APPEND sources
        "note4_book_transfer.cc"
        "note4_book_web.cc"
        "note4_book_web_esp.cc"
        "note4_book_web_data.S")
    list(APPEND private_requires esp_http_server)
endif()
idf_component_register(
    SRCS ${sources}
    INCLUDE_DIRS "include"
    REQUIRES
        note4_log
        bt
        esp_system
        esp_timer
        note4_companion
        note4_nfc_service
        note4_storage
        note4_time
    PRIV_REQUIRES ${private_requires}
    KCONFIG_PROJBUILD "${CMAKE_CURRENT_LIST_DIR}/../../cmake/Kconfig.empty"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
if(CONFIG_NOTE4_ENABLE_WIFI_HTTP)
    # IDF 5.5 logs the complete outgoing headers at DEBUG, including Bearer
    # tokens. Keep its packet logs compiled out even if product DEBUG is enabled.
    # INFO/WARN/ERROR diagnostics remain available; TLS validation is unchanged.
    idf_component_get_property(http_client_lib esp_http_client COMPONENT_LIB)
    target_compile_definitions(${http_client_lib} PRIVATE LOG_LOCAL_LEVEL=ESP_LOG_INFO)
endif()
if(CONFIG_NOTE4_ENABLE_BOOK_TRANSFER)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        NOTE4_BOOK_WEB_PATH="${CMAKE_CURRENT_SOURCE_DIR}/web/books.html")
    set_source_files_properties(note4_book_web_data.S PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/web/books.html")
endif()
