include("${CMAKE_CURRENT_LIST_DIR}/../cmake/zectrix_features.cmake")

set(requires zectrix_demo_ui zectrix_app zectrix_display zectrix_input
    zectrix_power zectrix_platform zectrix_self_test zectrix_storage
    zectrix_system zectrix_time esp_psram)
if(CONFIG_ZECTRIX_ENABLE_CONNECTIVITY)
    list(APPEND requires zectrix_connectivity)
endif()
if(CONFIG_ZECTRIX_ENABLE_READER)
    list(APPEND requires zectrix_reader)
endif()
if(CONFIG_ZECTRIX_ENABLE_USB_CLI)
    list(APPEND requires zectrix_cli)
endif()
if(CONFIG_ZECTRIX_ENABLE_UPDATE)
    list(APPEND requires zectrix_update)
endif()

idf_component_register(SRCS "app_main.cc"
                       REQUIRES ${requires}
                       EMBED_FILES
                       "assets/lighthouse_400x300_1bpp.bin"
                       "assets/snow_path_400x300_1bpp.bin"
                       "assets/footprint_animation.bin"
                       "assets/mountain_400x300_4bpp.bin")

if(CONFIG_ZECTRIX_ENABLE_BOOK_STORAGE)
    set(ZECTRIX_BOOKS_DIR "${PROJECT_DIR}/books" CACHE PATH "Directory of UTF-8 TXT and EPUB books")
    # The explicit books-flash target installs content; normal firmware flash preserves it.
    spiffs_create_partition_image(books "${ZECTRIX_BOOKS_DIR}")
endif()
