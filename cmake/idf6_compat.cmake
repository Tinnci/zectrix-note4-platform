# IDF 6.0.3 NimBLE sorts adjacent NVS bond entries without a compile-visible
# capacity bound. Our single-bond configuration triggers GCC array-bounds.
# Compile a capacity-bounded copy, retaining SDK checkout and security policy.
if(IDF_VERSION_MAJOR EQUAL 6 AND IDF_VERSION_MINOR EQUAL 0 AND CONFIG_NOTE4_ENABLE_CONNECTIVITY)
    idf_component_get_property(note4_bt_lib bt COMPONENT_LIB)
    get_target_property(note4_bt_sources ${note4_bt_lib} SOURCES)
    set(note4_bt_replacements)
    foreach(source IN LISTS note4_bt_sources)
        if(source MATCHES "/ble_store_nvs[.]c$")
            get_filename_component(source_directory "${source}" DIRECTORY)
            target_include_directories(${note4_bt_lib} PRIVATE "${source_directory}")
            file(READ "${source}" text)
            string(REPLACE "for (int i = 0; i < check_limit; i++) {"
                "for (int i = 0; i < check_limit && i + 1 < MYNEWT_VAL(BLE_STORE_MAX_BONDS); i++) {" text "${text}")
            set(copy "${CMAKE_BINARY_DIR}/note4-compat/ble_store_nvs.c")
            file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/note4-compat")
            file(WRITE "${copy}" "${text}")
            list(APPEND note4_bt_replacements "${copy}")
        else()
            list(APPEND note4_bt_replacements "${source}")
        endif()
    endforeach()
    set_property(TARGET ${note4_bt_lib} PROPERTY SOURCES "${note4_bt_replacements}")
endif()
