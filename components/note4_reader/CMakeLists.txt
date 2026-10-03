include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/note4_features.cmake")
set(private_requires)
if(CONFIG_NOTE4_ENABLE_CONNECTIVITY)
    list(APPEND private_requires note4_connectivity)
endif()
idf_component_register(
    SRCS "note4_reader.cc" "note4_reader_zip.cc" "note4_reader_text.cc"
         "note4_reader_font.cc" "note4_reader_font_data.S"
         "note4_reader_bookmarks.cc" "note4_reader_platform.cc"
         "third_party/miniz/miniz_tinfl.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private" "third_party/miniz"
    REQUIRES note4_storage note4_text
    PRIV_REQUIRES ${private_requires}
    KCONFIG_PROJBUILD "${CMAKE_CURRENT_LIST_DIR}/../../cmake/Kconfig.empty"
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    NOTE4_READER_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/font/reader_font.bin")
set_source_files_properties(note4_reader_font_data.S PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/font/reader_font.bin")
