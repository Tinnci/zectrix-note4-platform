include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/note4_features.cmake")
set(sources "note4_app_contract.cc" "note4_application_runtime.cc"
         "note4_sdk_status.cc"
         "note4_first_party_app_controllers.cc"
         "note4_launcher_controller.cc"
         "note4_scene_manager.cc"
         "note4_gallery_controller.cc"
         "note4_locale.cc" "note4_language_setting.cc"
         "note4_sleep_cover.cc")
set(requires note4_log note4_power note4_time note4_storage note4_text)
if(CONFIG_NOTE4_ENABLE_UTILITIES)
    list(APPEND sources "note4_utilities.cc")
endif()
if(CONFIG_NOTE4_ENABLE_RUNTIME)
    list(APPEND sources "note4_micro_app_controller.cc")
    list(APPEND requires note4_runtime)
endif()
if(CONFIG_NOTE4_ENABLE_USB_HOST)
    list(APPEND sources "note4_usb_manager.cc")
    list(APPEND requires note4_host)
endif()
if(CONFIG_NOTE4_ENABLE_READER)
    list(APPEND sources "note4_reader_controller.cc")
    list(APPEND requires note4_reader)
endif()
if(CONFIG_NOTE4_ENABLE_BOOK_TRANSFER)
    list(APPEND sources "note4_book_transfer_controller.cc")
    list(APPEND requires note4_connectivity)
endif()
idf_component_register(SRCS ${sources} INCLUDE_DIRS "include" REQUIRES ${requires})

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
