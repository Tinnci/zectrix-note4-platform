idf_component_register(
    SRCS "zectrix_display_state.cc" "zectrix_display_physics.cc" "zectrix_display_service.cc"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES zectrix_epd esp_timer
)
