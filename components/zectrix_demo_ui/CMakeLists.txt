include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/zectrix_features.cmake")
set(sources "zectrix_canvas.cc" "zectrix_demo_ui.cc" "zectrix_view_port.cc"
    "zectrix_status_bar.cc" "zectrix_sleep_ui.cc" "zectrix_unicode_text.cc")
set(requires zectrix_display zectrix_power zectrix_self_test zectrix_system
    zectrix_time zectrix_app)
if(CONFIG_ZECTRIX_ENABLE_READER)
    list(APPEND sources "zectrix_reader_ui.cc")
    list(APPEND requires zectrix_reader)
endif()
if(CONFIG_ZECTRIX_ENABLE_BOOK_TRANSFER)
    list(APPEND sources "zectrix_book_transfer_ui.cc")
    list(APPEND requires zectrix_connectivity)
endif()
idf_component_register(
    SRCS ${sources}
    INCLUDE_DIRS "." "include" "font"
    REQUIRES ${requires}
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
