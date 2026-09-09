idf_component_register(
    SRCS "zectrix_reader.cc" "zectrix_reader_zip.cc" "zectrix_reader_text.cc"
         "zectrix_reader_font.cc" "zectrix_reader_font_data.S"
         "zectrix_reader_bookmarks.cc" "third_party/miniz/miniz_tinfl.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private" "third_party/miniz"
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ZECTRIX_READER_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/font/reader_font.bin")
set_source_files_properties(zectrix_reader_font_data.S PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/font/reader_font.bin")
