include(FetchContent)
set(ZECTRIX_LUA_SOURCE_DIR "" CACHE PATH "Existing Lua 5.4.9 source tree (optional)")
if(NOT ZECTRIX_LUA_SOURCE_DIR)
    FetchContent_Declare(note4_lua_source
        GIT_REPOSITORY https://github.com/lua/lua.git
        GIT_TAG v5.4.9
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(note4_lua_source)
    set(ZECTRIX_LUA_SOURCE_DIR "${note4_lua_source_SOURCE_DIR}")
endif()
set(lua_sources lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c
    lgc.c llex.c lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c
    ltable.c ltm.c lundump.c lvm.c lzio.c lauxlib.c)
list(TRANSFORM lua_sources PREPEND "${ZECTRIX_LUA_SOURCE_DIR}/")
add_library(note4_lua STATIC ${lua_sources})
target_include_directories(note4_lua PUBLIC "${ZECTRIX_LUA_SOURCE_DIR}")
# Limit recursive parser/C calls for the shell's 8 KiB native stack. No standard
# libraries, dynamic C modules or bytecode-loading entry point are exposed.
target_compile_definitions(note4_lua PRIVATE LUAI_MAXCCALLS=16)
target_compile_options(note4_lua PRIVATE -Os -ffunction-sections -fdata-sections)
target_link_libraries(note4_lua PUBLIC m)
