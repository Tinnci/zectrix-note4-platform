idf_component_register(SRCS "app_main.cc"
                       REQUIRES
                       zectrix_demo_ui
                       zectrix_app
                       zectrix_display
                       zectrix_input
                       zectrix_power
                       zectrix_platform
                       zectrix_self_test
                       zectrix_storage
                       zectrix_system
                       zectrix_time
                       EMBED_FILES
                       "assets/lighthouse_400x300_1bpp.bin"
                       "assets/snow_path_400x300_1bpp.bin"
                       "assets/footprint_animation.bin"
                       "assets/mountain_400x300_4bpp.bin")
