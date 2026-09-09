idf_component_register(SRCS "app_main.cc"
                       REQUIRES
                       zectrix_demo_ui
                       zectrix_app
                       zectrix_connectivity
                       zectrix_display
                       zectrix_input
                       zectrix_power
                       zectrix_reader
                       zectrix_platform
                       zectrix_self_test
                       zectrix_storage
                       zectrix_system
                       zectrix_time
                       zectrix_update
                       esp_psram
                       EMBED_FILES
                       "assets/lighthouse_400x300_1bpp.bin"
                       "assets/snow_path_400x300_1bpp.bin"
                       "assets/footprint_animation.bin"
                       "assets/mountain_400x300_4bpp.bin")

set(ZECTRIX_BOOKS_DIR "${PROJECT_DIR}/books" CACHE PATH "Directory of UTF-8 TXT and EPUB books")
# The explicit books-flash target installs content; normal firmware flash preserves it.
spiffs_create_partition_image(books "${ZECTRIX_BOOKS_DIR}")
