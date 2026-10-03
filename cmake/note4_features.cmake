# ESP-IDF expands REQUIRES before generating sdkconfig.cmake. Import the same
# Kconfig selections for that first pass; normal registration uses IDF's config.
if(CMAKE_BUILD_EARLY_EXPANSION)
    idf_build_get_property(note4_features_file NOTE4_FEATURES_FILE)
    include("${note4_features_file}")
endif()
