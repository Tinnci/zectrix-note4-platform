include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/zectrix_features.cmake")
set(sources "zectrix_app_contract.cc" "zectrix_application_runtime.cc"
         "zectrix_sdk_status.cc"
         "zectrix_first_party_app_controllers.cc"
         "zectrix_launcher_controller.cc"
         "zectrix_scene_manager.cc"
         "zectrix_gallery_controller.cc"
         "zectrix_sleep_cover.cc")
set(requires zectrix_power zectrix_time)
if(CONFIG_ZECTRIX_ENABLE_READER)
    list(APPEND sources "zectrix_reader_controller.cc")
    list(APPEND requires zectrix_reader)
endif()
if(CONFIG_ZECTRIX_ENABLE_BOOK_TRANSFER)
    list(APPEND sources "zectrix_book_transfer_controller.cc")
    list(APPEND requires zectrix_connectivity)
endif()
idf_component_register(SRCS ${sources} INCLUDE_DIRS "include" REQUIRES ${requires})

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
