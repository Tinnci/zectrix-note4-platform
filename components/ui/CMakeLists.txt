include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/zectrix_features.cmake")
set(COMPONENT_KCONFIG_PROJBUILD "${CMAKE_CURRENT_LIST_DIR}/../../cmake/Kconfig.empty")
set(sources "canvas.cc" "ui_engine.cc" "view_port.cc"
    "status_bar.cc" "sleep_ui.cc" "unicode_text.cc"
    "launcher_ui.cc")
set(requires zectrix_display zectrix_power zectrix_self_test zectrix_system
    zectrix_time zectrix_app zectrix_text)
if(CONFIG_ZECTRIX_ENABLE_UTILITIES)
    list(APPEND sources "utilities_ui.cc")
endif()
if(CONFIG_ZECTRIX_ENABLE_RUNTIME)
    list(APPEND sources "micro_app_ui.cc" "micro_app_view.cc")
    list(APPEND requires zectrix_runtime)
endif()
if(CONFIG_ZECTRIX_ENABLE_USB_HOST)
    list(APPEND sources "usb_manager_ui.cc")
    list(APPEND requires zectrix_host)
endif()
if(CONFIG_ZECTRIX_ENABLE_READER)
    list(APPEND sources "reader_ui.cc")
    list(APPEND requires zectrix_reader)
endif()
if(CONFIG_ZECTRIX_ENABLE_BOOK_TRANSFER)
    list(APPEND sources "book_transfer_ui.cc")
    list(APPEND requires zectrix_connectivity)
endif()
idf_component_register(
    SRCS ${sources}
    INCLUDE_DIRS "." "include" "font"
    REQUIRES ${requires}
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
