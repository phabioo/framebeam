# framebeam_emulation: EmulatorBackend, LibretroBackend (core loaded via QLibrary, no linking),
# EmulationRunner, system manifests (Qt resource) and CoreLocator.
add_library(framebeam_emulation STATIC
  emulation_runner.cpp
  core_locator.cpp
  libretro_backend.cpp
  system_manifest.cpp
  firmware_materializer.cpp)
qt_add_resources(framebeam_emulation framebeam_manifests
  PREFIX "/framebeam/emulation"
  FILES manifests/nds.json)
target_include_directories(framebeam_emulation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(framebeam_emulation SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_compile_features(framebeam_emulation PUBLIC cxx_std_20)
target_link_libraries(framebeam_emulation PUBLIC Qt6::Core Qt6::Gui ${CMAKE_DL_LIBS})
framebeam_warnings(framebeam_emulation)

if(BUILD_TESTING)
  # Generator for the homebrew test ROM (plain C++, also runs on Windows CI).
  add_executable(framebeam_make_test_rom tools/make_test_rom.cpp)
  framebeam_warnings(framebeam_make_test_rom)

  # The test ROM is generated only in the build directory, never in the repo.
  set(FB_TEST_ROM ${CMAKE_CURRENT_BINARY_DIR}/framebeam_test.nds)
  add_custom_command(OUTPUT ${FB_TEST_ROM}
    COMMAND framebeam_make_test_rom ${FB_TEST_ROM}
    DEPENDS framebeam_make_test_rom
    COMMENT "Generating NDS homebrew test ROM"
    VERBATIM)
  add_custom_target(framebeam_test_rom ALL DEPENDS ${FB_TEST_ROM})

  # Without core: manifest parsing, CoreLocator.
  framebeam_add_test(emulation_manifest tests/manifest_test.cpp LIBS framebeam_emulation)
  target_compile_definitions(test_emulation_manifest PRIVATE FB_MANIFEST_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/manifests")

  framebeam_add_test(emulation_firmware tests/firmware_test.cpp LIBS framebeam_emulation)

  # Fake libretro core (SAVE_RAM only) for the save persistence tests.
  add_library(framebeam_fake_core MODULE tests/fake_core.cpp)
  target_include_directories(framebeam_fake_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  set_target_properties(framebeam_fake_core PROPERTIES PREFIX "" OUTPUT_NAME framebeam_fake_core)
  framebeam_add_test(emulation_save_ram tests/save_ram_test.cpp LIBS framebeam_emulation)
  target_compile_definitions(test_emulation_save_ram PRIVATE FB_FAKE_CORE_PATH="$<TARGET_FILE:framebeam_fake_core>")
  add_dependencies(test_emulation_save_ram framebeam_fake_core)

  # With core (SKIP/77 without FRAMEBEAM_MELONDS_DS_CORE).
  framebeam_add_test(emulation_core tests/core_test.cpp LIBS framebeam_emulation NEEDS_CORE)
  target_compile_definitions(test_emulation_core PRIVATE FB_TEST_ROM_PATH="${FB_TEST_ROM}")
  add_dependencies(test_emulation_core framebeam_test_rom)
endif()
