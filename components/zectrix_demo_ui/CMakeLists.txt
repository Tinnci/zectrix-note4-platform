idf_component_register(
    SRCS "zectrix_canvas.cc" "zectrix_demo_ui.cc"
    INCLUDE_DIRS "." "include" "font"
    REQUIRES zectrix_display zectrix_power zectrix_self_test zectrix_time
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
