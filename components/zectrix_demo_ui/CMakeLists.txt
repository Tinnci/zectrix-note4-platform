idf_component_register(
    SRCS "zectrix_canvas.cc" "zectrix_demo_ui.cc" "zectrix_view_port.cc"
         "zectrix_status_bar.cc" "zectrix_reader_ui.cc"
    INCLUDE_DIRS "." "include" "font"
    REQUIRES zectrix_display zectrix_power zectrix_self_test zectrix_system zectrix_time
             zectrix_app zectrix_reader
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
