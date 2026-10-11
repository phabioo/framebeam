# framebeam_formats: generic game-file format helpers (LZ10/LZ11, GARC, 3DS RomFS, CTR textures, PK6 crypto)
# that the plugin runtime exposes to Lua plugins (docs/architecture/12-plugins.md 16.3). No game knowledge.
add_library(framebeam_formats STATIC
  lz.cpp
  garc.cpp
  romfs.cpp
  texture.cpp
  pk6.cpp)
target_include_directories(framebeam_formats PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(framebeam_formats PUBLIC cxx_std_20)
target_link_libraries(framebeam_formats PUBLIC Qt6::Core Qt6::Gui)
framebeam_warnings(framebeam_formats)

framebeam_add_test(formats_lz tests/lz_test.cpp LIBS framebeam_formats)
framebeam_add_test(formats_garc tests/garc_test.cpp LIBS framebeam_formats)
framebeam_add_test(formats_romfs tests/romfs_test.cpp LIBS framebeam_formats)
framebeam_add_test(formats_texture tests/texture_test.cpp LIBS framebeam_formats)
framebeam_add_test(formats_pk6 tests/pk6_test.cpp LIBS framebeam_formats)
