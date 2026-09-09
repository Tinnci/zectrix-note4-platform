idf_component_register(
    SRCS "zectrix_app_contract.cc" "zectrix_application_runtime.cc"
         "zectrix_sdk_status.cc"
         "zectrix_first_party_app_controllers.cc"
         "zectrix_scene_manager.cc"
         "zectrix_gallery_controller.cc"
         "zectrix_reader_controller.cc"
    INCLUDE_DIRS "include"
    REQUIRES zectrix_reader
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
