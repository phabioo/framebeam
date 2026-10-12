# framebeam_plugins: plugin runtime of the Player (docs/architecture/12-plugins.md 16.3-16.7, ADR 0023):
# manifests, plugin manager, Lua sandbox and plugin host on a dedicated plugin thread. No UI, no Hub.

# Vendored Lua 5.4.7, compiled as C++ (lua_error unwinds with C++ exceptions, destructors run). No warning flags.
set(FB_LUA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua-5.4.7)
set(FB_LUA_SOURCES
  lapi.c lauxlib.c lbaselib.c lcode.c lcorolib.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c llex.c lmathlib.c
  lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c lstrlib.c ltable.c ltablib.c ltm.c lundump.c lutf8lib.c
  lvm.c lzio.c)
list(TRANSFORM FB_LUA_SOURCES PREPEND ${FB_LUA_DIR}/)
add_library(framebeam_lua STATIC ${FB_LUA_SOURCES})
set_source_files_properties(${FB_LUA_SOURCES} PROPERTIES LANGUAGE CXX)
target_include_directories(framebeam_lua PUBLIC ${FB_LUA_DIR})
target_compile_features(framebeam_lua PUBLIC cxx_std_20)
if(MSVC)
  target_compile_definitions(framebeam_lua PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(framebeam_lua PRIVATE /w)
else()
  target_compile_options(framebeam_lua PRIVATE -w)
endif()
set_target_properties(framebeam_lua PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(framebeam_plugins STATIC manifest.cpp plugin_value.cpp lua_sandbox.cpp plugin_engine.cpp plugin_ui.cpp plugin_api.cpp
  plugin_host.cpp plugin_manager.cpp)
target_include_directories(framebeam_plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(framebeam_plugins PUBLIC cxx_std_20)
target_link_libraries(framebeam_plugins PUBLIC framebeam_lua framebeam_core framebeam_formats Qt6::Core)
framebeam_warnings(framebeam_plugins)

framebeam_add_test(plugins_manifest tests/manifest_test.cpp LIBS framebeam_plugins)
framebeam_add_test(plugins_sandbox tests/sandbox_test.cpp LIBS framebeam_plugins)
framebeam_add_test(plugins_manager tests/manager_test.cpp LIBS framebeam_plugins)
framebeam_add_test(plugins_host tests/host_test.cpp LIBS framebeam_plugins)
framebeam_add_test(plugins_ui tests/ui_test.cpp LIBS framebeam_plugins)
