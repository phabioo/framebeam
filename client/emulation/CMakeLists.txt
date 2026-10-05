# framebeam_emulation: EmulatorBackend, LibretroBackend (Core per QLibrary, kein Link),
# EmulationRunner, System-Manifeste (Qt-Ressource) und CoreLocator.
add_library(framebeam_emulation STATIC
  emulation_runner.cpp
  core_locator.cpp
  libretro_backend.cpp
  system_manifest.cpp)
qt_add_resources(framebeam_emulation framebeam_manifests
  PREFIX "/framebeam/emulation"
  FILES manifests/nds.json)
target_include_directories(framebeam_emulation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(framebeam_emulation SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_compile_features(framebeam_emulation PUBLIC cxx_std_20)
target_link_libraries(framebeam_emulation PUBLIC Qt6::Core Qt6::Gui ${CMAKE_DL_LIBS})
framebeam_warnings(framebeam_emulation)

if(BUILD_TESTING)
  # Generator fuer die Homebrew-Test-ROM (reines C++, laeuft auch unter Windows-CI).
  add_executable(framebeam_make_test_rom tools/make_test_rom.cpp)
  framebeam_warnings(framebeam_make_test_rom)

  # Test-ROM entsteht nur im Build-Verzeichnis, nie im Repo.
  set(FB_TEST_ROM ${CMAKE_CURRENT_BINARY_DIR}/framebeam_test.nds)
  add_custom_command(OUTPUT ${FB_TEST_ROM}
    COMMAND framebeam_make_test_rom ${FB_TEST_ROM}
    DEPENDS framebeam_make_test_rom
    COMMENT "Erzeuge NDS-Homebrew-Test-ROM"
    VERBATIM)
  add_custom_target(framebeam_test_rom ALL DEPENDS ${FB_TEST_ROM})

  # Ohne Core: Manifest-Parsing, CoreLocator.
  framebeam_add_test(emulation_manifest tests/manifest_test.cpp LIBS framebeam_emulation)
  target_compile_definitions(test_emulation_manifest PRIVATE FB_MANIFEST_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/manifests")

  # Mit Core (SKIP/77 ohne FRAMEBEAM_MELONDS_DS_CORE).
  framebeam_add_test(emulation_core tests/core_test.cpp LIBS framebeam_emulation NEEDS_CORE)
  target_compile_definitions(test_emulation_core PRIVATE FB_TEST_ROM_PATH="${FB_TEST_ROM}")
  add_dependencies(test_emulation_core framebeam_test_rom)
endif()
