idf_component_register(SRCS "app_main.cc"
                       REQUIRES
                       zectrix_board
                       zectrix_demo_ui
                       zectrix_epd
                       zectrix_self_test
                       esp_timer
                       heap
                       spi_flash
                       EMBED_FILES
                       "assets/lighthouse_400x300_1bpp.bin"
                       "assets/snow_path_400x300_1bpp.bin"
                       "assets/footprint_animation.bin"
                       "assets/mountain_400x300_4bpp.bin")
