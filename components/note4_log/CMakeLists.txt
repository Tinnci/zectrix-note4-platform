idf_component_register(SRCS "note4_log.cc" "note4_log_esp.cc"
    INCLUDE_DIRS "include" REQUIRES log)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
# Project consumers retain DEBUG without expanding every SDK component's logs.
target_compile_definitions(${COMPONENT_LIB} INTERFACE LOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
