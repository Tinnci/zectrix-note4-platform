idf_component_register(
    SRCS "note4_display_state.cc" "note4_display_physics.cc" "note4_display_service.cc" "note4_display_model.cc"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES note4_log note4_epd esp_timer
)
