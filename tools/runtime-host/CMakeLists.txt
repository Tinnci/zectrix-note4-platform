cmake_minimum_required(VERSION 3.16)
project(note4_runtime_host C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
get_filename_component(repo "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${repo}/components/zectrix_runtime/lua.cmake")
add_library(note4_runtime "${repo}/components/zectrix_runtime/zectrix_runtime.cc")
target_include_directories(note4_runtime PUBLIC "${repo}/components/zectrix_runtime/include"
    "${repo}/components/zectrix_text/include")
target_compile_options(note4_runtime PRIVATE -Wall -Wextra -Werror)
target_link_libraries(note4_runtime PRIVATE note4_lua)
add_executable(runtime_test "${repo}/tools/runtime_test.cc")
target_compile_options(runtime_test PRIVATE -Wall -Wextra -Werror)
target_link_libraries(runtime_test PRIVATE note4_runtime)
enable_testing()
add_test(NAME runtime COMMAND runtime_test "${repo}/apps")
set_tests_properties(runtime PROPERTIES TIMEOUT 30)

add_executable(micro_app_test "${repo}/tools/micro_app_test.cc"
    "${repo}/components/zectrix_app/zectrix_micro_app_controller.cc"
    "${repo}/components/zectrix_app/zectrix_scene_manager.cc"
    "${repo}/components/zectrix_storage/zectrix_book_storage.cc"
    "${repo}/components/zectrix_demo_ui/zectrix_micro_app_view.cc"
    "${repo}/components/zectrix_demo_ui/zectrix_canvas.cc")
target_include_directories(micro_app_test PRIVATE "${repo}/tools/host_include"
    "${repo}/components/zectrix_app/include" "${repo}/components/zectrix_storage/include"
    "${repo}/components/zectrix_demo_ui/include" "${repo}/components/zectrix_demo_ui/font")
target_compile_definitions(micro_app_test PRIVATE CONFIG_ZECTRIX_ENABLE_READER=0)
target_compile_options(micro_app_test PRIVATE -Wall -Wextra -Werror)
target_link_libraries(micro_app_test PRIVATE note4_runtime)
add_test(NAME micro_apps COMMAND micro_app_test "${repo}/apps" "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(micro_apps PROPERTIES TIMEOUT 30)
