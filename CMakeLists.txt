cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include($ENV{IDF_PATH}/tools/cmake/version.cmake)
include(cmake/configure_features.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    # Keep SDK-specific resolution out of the default 5.5 lockfile.
    set(DEPENDENCIES_LOCK "${CMAKE_BINARY_DIR}/dependencies.lock")
    idf_build_set_property(DEPENDENCIES_LOCK "${DEPENDENCIES_LOCK}")
endif()
project(note4_platform)
include(cmake/idf6_compat.cmake)
