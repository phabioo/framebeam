# framebeam_emulation: EmulatorBackend, LibretroBackend (core loaded via QLibrary, no linking),
# EmulationRunner, system manifests (Qt resource) and CoreLocator.
add_library(framebeam_emulation STATIC
  emulation_runner.cpp
  frame_stats.cpp
  core_locator.cpp
  libretro_backend.cpp
  system_manifest.cpp
  firmware_materializer.cpp
  core_options.cpp
  hw_render.cpp
  memory_map.cpp
  memory_snapshot.cpp
  game_identity.cpp)
# Systems and core profiles are data only (ADR 0020 D6): a new manifests/{systems,cores}/*.json needs no CMake edit.
file(GLOB FB_MANIFEST_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS manifests/systems/*.json manifests/cores/*.json)
qt_add_resources(framebeam_emulation framebeam_manifests
  PREFIX "/framebeam/emulation"
  FILES ${FB_MANIFEST_FILES})
target_include_directories(framebeam_emulation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(framebeam_emulation SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_compile_features(framebeam_emulation PUBLIC cxx_std_20)
target_link_libraries(framebeam_emulation PUBLIC framebeam_inputmap Qt6::Core Qt6::Gui ${CMAKE_DL_LIBS})
if(WIN32)
  target_link_libraries(framebeam_emulation PRIVATE winmm)
endif()
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

  framebeam_add_test(emulation_frame_stats tests/frame_stats_test.cpp LIBS framebeam_emulation)
  framebeam_add_test(emulation_firmware tests/firmware_test.cpp LIBS framebeam_emulation)
  framebeam_add_test(emulation_launch_options tests/launch_options_test.cpp LIBS framebeam_emulation)
  framebeam_add_test(emulation_core_options tests/core_options_test.cpp LIBS framebeam_emulation NEEDS_CORE)
  framebeam_add_test(emulation_core_options_testcore tests/core_options_test.cpp LIBS framebeam_emulation TEST_CORE)

  # Plugin memory access: pure map/identity tests, and snapshots against the test core's synthetic memory map.
  framebeam_add_test(emulation_memory_map tests/memory_map_test.cpp LIBS framebeam_emulation)
  framebeam_add_test(emulation_game_identity tests/game_identity_test.cpp LIBS framebeam_emulation)

  # Test-only libretro MODULEs. They link no vcpkg DLLs, so skip vcpkg's z-applocal post-link step (it is attached in
  # the toolchain's add_library override and read at call time, hence the function-local override). With it, Ninja
  # runs one z-applocal per module in parallel on Windows and they race on the shared vcpkg bin directory ("The
  # process cannot access the file because it is being used by another process"). No-op off Windows/vcpkg.
  function(framebeam_add_test_module target)
    set(VCPKG_APPLOCAL_DEPS OFF)
    add_library(${target} MODULE ${ARGN})
  endfunction()

  # Fake libretro core (SAVE_RAM only) for the save persistence tests.
  framebeam_add_test_module(framebeam_fake_core tests/fake_core.cpp)
  target_include_directories(framebeam_fake_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  set_target_properties(framebeam_fake_core PROPERTIES PREFIX "" OUTPUT_NAME framebeam_fake_core)
  framebeam_add_test(emulation_save_ram tests/save_ram_test.cpp LIBS framebeam_emulation)
  target_compile_definitions(test_emulation_save_ram PRIVATE FB_FAKE_CORE_PATH="$<TARGET_FILE:framebeam_fake_core>")
  add_dependencies(test_emulation_save_ram framebeam_fake_core)
  framebeam_add_test(emulation_frame_pacer tests/frame_pacer_test.cpp LIBS framebeam_emulation)
  framebeam_add_test(emulation_fast_forward tests/fast_forward_test.cpp LIBS framebeam_emulation)
  target_compile_definitions(test_emulation_fast_forward PRIVATE FB_FAKE_CORE_PATH="$<TARGET_FILE:framebeam_fake_core>")
  add_dependencies(test_emulation_fast_forward framebeam_fake_core)

  # Fake cores for the probe tests: with and without SET_SUPPORT_NO_GAME (the latter crashes on retro_load_game(NULL)).
  foreach(variant IN ITEMS 0 1)
    set(tgt framebeam_fake_nogame_core_${variant})
    framebeam_add_test_module(${tgt} tests/fake_nogame_core.cpp)
    target_include_directories(${tgt} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(${tgt} PRIVATE FB_DECLARE_NO_GAME=${variant})
    set_target_properties(${tgt} PROPERTIES PREFIX "" OUTPUT_NAME ${tgt})
  endforeach()
  framebeam_add_test(emulation_core_probe tests/core_probe_test.cpp LIBS framebeam_emulation)
  target_compile_definitions(test_emulation_core_probe PRIVATE
    FB_FAKE_NOGAME_CORE_PATH="$<TARGET_FILE:framebeam_fake_nogame_core_0>"
    FB_FAKE_NOGAME_DECLARED_CORE_PATH="$<TARGET_FILE:framebeam_fake_nogame_core_1>")
  add_dependencies(test_emulation_core_probe framebeam_fake_nogame_core_0 framebeam_fake_nogame_core_1)

  # Fake libretro core with OpenGL hardware rendering + test (needs QGuiApplication and a GL 3.3 context; SKIP/77
  # without one, except on Linux CI where it must run). Runs on the offscreen QPA with GLX (under xvfb) or EGL.
  framebeam_add_test_module(framebeam_fake_hw_core tests/fake_hw_core.cpp)
  target_include_directories(framebeam_fake_hw_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  set_target_properties(framebeam_fake_hw_core PROPERTIES PREFIX "" OUTPUT_NAME framebeam_fake_hw_core)
  framebeam_add_test(emulation_hw_render tests/hw_render_test.cpp LIBS framebeam_emulation Qt6::Gui)
  target_compile_definitions(test_emulation_hw_render PRIVATE FB_FAKE_HW_CORE_PATH="$<TARGET_FILE:framebeam_fake_hw_core>")
  add_dependencies(test_emulation_hw_render framebeam_fake_hw_core)
  set_tests_properties(emulation_hw_render PROPERTIES SKIP_RETURN_CODE 77)

  # FrameBeam test core (libretro MODULE, test only: not installed, not packaged, not part of the Player). Merge-gating
  # replacement for the real emulator cores in the tests below; see tests/test_core.cpp.
  framebeam_add_test_module(framebeam_test_core tests/test_core.cpp)
  target_include_directories(framebeam_test_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/tests ${FRAMEBEAM_TESTUTIL_DIR})
  set_target_properties(framebeam_test_core PROPERTIES PREFIX "" OUTPUT_NAME framebeam_test_core)
  framebeam_warnings(framebeam_test_core)
  framebeam_add_test(emulation_test_core tests/test_core_test.cpp LIBS framebeam_emulation TEST_CORE TEST_CORE_FLAVOR generic)
  target_include_directories(test_emulation_test_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  target_compile_definitions(test_emulation_test_core PRIVATE FB_TEST_CORE_PATH="$<TARGET_FILE:framebeam_test_core>")
  add_dependencies(test_emulation_test_core framebeam_test_rom)
  target_compile_definitions(test_emulation_test_core PRIVATE FB_TEST_ROM_PATH="${FB_TEST_ROM}")

  # Input mapping chain (FrameBeam input -> system profile -> libretro mask) ending in the test core, e.g. ZR -> R2.
  framebeam_add_test(emulation_test_core_input tests/test_core_input_test.cpp LIBS framebeam_emulation framebeam_input framebeam_sdl3 TEST_CORE TEST_CORE_FLAVOR generic)
  target_include_directories(test_emulation_test_core_input PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  target_compile_definitions(test_emulation_test_core_input PRIVATE FB_TEST_CORE_PATH="$<TARGET_FILE:framebeam_test_core>")

  # With core (SKIP/77 without FRAMEBEAM_MELONDS_DS_CORE).
  framebeam_add_test(emulation_core tests/core_test.cpp LIBS framebeam_emulation NEEDS_CORE)
  target_compile_definitions(test_emulation_core PRIVATE FB_TEST_ROM_PATH="${FB_TEST_ROM}")
  add_dependencies(test_emulation_core framebeam_test_rom)

  # DeSmuME smoke test (SKIP/77 without FRAMEBEAM_DESMUME_CORE).
  framebeam_add_test(emulation_core_desmume tests/core_desmume_test.cpp LIBS framebeam_emulation NEEDS_DESMUME_CORE)
  target_compile_definitions(test_emulation_core_desmume PRIVATE FB_TEST_ROM_PATH="${FB_TEST_ROM}")
  add_dependencies(test_emulation_core_desmume framebeam_test_rom)

  framebeam_add_test(emulation_core_speed tests/core_speed_test.cpp LIBS framebeam_emulation NEEDS_CORE)
  target_compile_definitions(test_emulation_core_speed PRIVATE FB_TEST_ROM_PATH="${FB_TEST_ROM}")
  add_dependencies(test_emulation_core_speed framebeam_test_rom)

  # melonDS DS OpenGL renderer through hardware-frame readback (SKIP/77 without core or GL 3.3 context).
  framebeam_add_test(emulation_core_gl tests/core_gl_test.cpp LIBS framebeam_emulation Qt6::Gui NEEDS_CORE)
  target_compile_definitions(test_emulation_core_gl PRIVATE FB_TEST_ROM_PATH="${FB_TEST_ROM}")
  add_dependencies(test_emulation_core_gl framebeam_test_rom)

  # The same tests against the FrameBeam test core (flavors with the option keys of the product core profiles): always
  # run, never SKIP (the GL one only without a GL 3.3 context). The real-core tests above run in the nightly workflow.
  foreach(variant IN ITEMS core core_speed core_gl)
    set(src tests/${variant}_test.cpp)
    set(libs framebeam_emulation)
    if(variant STREQUAL "core_gl")
      list(APPEND libs Qt6::Gui)
    endif()
    framebeam_add_test(emulation_${variant}_testcore ${src} LIBS ${libs} TEST_CORE)
    target_compile_definitions(test_emulation_${variant}_testcore PRIVATE FB_TEST_ROM_PATH="${FB_TEST_ROM}")
    add_dependencies(test_emulation_${variant}_testcore framebeam_test_rom)
  endforeach()
  set_tests_properties(emulation_core_gl_testcore PROPERTIES SKIP_RETURN_CODE 77)
  framebeam_add_test(emulation_core_desmume_testcore tests/core_desmume_test.cpp LIBS framebeam_emulation TEST_CORE TEST_CORE_FLAVOR desmume)
  target_compile_definitions(test_emulation_core_desmume_testcore PRIVATE FB_TEST_ROM_PATH="${FB_TEST_ROM}")
  add_dependencies(test_emulation_core_desmume_testcore framebeam_test_rom)

  framebeam_add_test(emulation_memory_snapshot_testcore tests/memory_snapshot_test.cpp LIBS framebeam_emulation TEST_CORE)
  target_include_directories(test_emulation_memory_snapshot_testcore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  target_compile_definitions(test_emulation_memory_snapshot_testcore PRIVATE FB_TEST_ROM_PATH="${FB_TEST_ROM}")
  add_dependencies(test_emulation_memory_snapshot_testcore framebeam_test_rom)
endif()
