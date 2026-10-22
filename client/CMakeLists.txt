cmake_minimum_required(VERSION 3.25)

# The product version X.Y.Z comes from the root VERSION file (shared with the Hub, see docs/roadmap.md 0.3).
file(STRINGS ${CMAKE_CURRENT_LIST_DIR}/../VERSION _fb_version_line LIMIT_COUNT 1)
string(STRIP "${_fb_version_line}" _fb_version_core)
if(NOT _fb_version_core MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "VERSION must contain X.Y.Z, found '${_fb_version_core}'")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/../VERSION)
project(Uzume VERSION ${_fb_version_core} LANGUAGES CXX)

# Build identity (CI sets it, see S1): full SemVer version, update channel (stable|beta|dev; "test" is an alias of beta), commit.
# Empty UZUME_VERSION = "<VERSION>-dev".
set(UZUME_VERSION "" CACHE STRING "Full Player version (SemVer); empty: <VERSION file>-dev")
set(UZUME_CHANNEL "dev" CACHE STRING "Compiled default update channel: stable | beta | dev (test = alias of beta)")
set(UZUME_COMMIT "" CACHE STRING "Commit SHA of the build")
if(UZUME_VERSION STREQUAL "")
  set(UZUME_EFFECTIVE_VERSION "${PROJECT_VERSION}-dev")
else()
  set(UZUME_EFFECTIVE_VERSION "${UZUME_VERSION}")
endif()
if(UZUME_CHANNEL STREQUAL "test")
  set(UZUME_CHANNEL "beta")
endif()
if(NOT UZUME_CHANNEL MATCHES "^(stable|beta|dev)$")
  message(FATAL_ERROR "UZUME_CHANNEL must be stable, beta or dev")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt >= 6.4 does NOT come from vcpkg: Linux apt (noble: 6.4.2), Windows install-qt-action (6.8 LTS).
# Code must work with the Qt 6.4 API.
find_package(OpenSSL REQUIRED)  # Ed25519 verification of the update index (Linux apt libssl-dev, Windows vcpkg)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Concurrent Network Gui Quick QuickControls2 QuickTest Multimedia WebSockets Test)
qt_standard_project_setup()
set(CMAKE_AUTOMOC ON)

# Path to the libretro core melonDS DS (.so/.dll), fetched from the libretro buildbot by `make fetch-core` (nightly workflow, ADR 0020).
# Empty: everything builds, tests with NEEDS_CORE are skipped by ctest (SKIP), not failed.
set(UZUME_MELONDS_DS_CORE "" CACHE FILEPATH "Path to the melonDS DS core library (.so/.dll)")
set(UZUME_DESMUME_CORE "" CACHE FILEPATH "Path to the DeSmuME core library (.so/.dll); empty = DeSmuME tests are skipped")

include(CTest)

function(uzume_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra)
  endif()
endfunction()

# uzume_add_test(<name> <sources...> [LIBS <libs...>] [NEEDS_CORE])
# QtTest/plain test as executable test_<name>, ctest name <name>, runs with QT_QPA_PLATFORM=offscreen.
# NEEDS_CORE: passes UZUME_MELONDS_DS_CORE as an environment variable; if the core is not set,
# the test ends with return code 77 (= SKIP). The test itself must then return 77.
# NEEDS_DESMUME_CORE: same with UZUME_DESMUME_CORE.
# TEST_CORE [TEST_CORE_FLAVOR <generic|melondsds|desmume>]: runs the test against the in-repo Uzume test core
# (emulation/tests/test_core.cpp) instead of a real emulator: UZUME_MELONDS_DS_CORE (flavor desmume:
# UZUME_DESMUME_CORE) points to it, so the product code finds it like a real core; never SKIPs; compiled with
# FB_TEST_CORE=1. Merge-gating CI uses these variants, the real-core tests (NEEDS_*) run in the nightly workflow.
set(UZUME_TESTUTIL_DIR ${CMAKE_CURRENT_LIST_DIR}/testutil)
function(uzume_add_test name)
  cmake_parse_arguments(ARG "NEEDS_CORE;NEEDS_DESMUME_CORE;TEST_CORE" "TEST_CORE_FLAVOR" "LIBS" ${ARGN})
  if(NOT BUILD_TESTING)
    return()
  endif()
  add_executable(test_${name} ${ARG_UNPARSED_ARGUMENTS})
  target_link_libraries(test_${name} PRIVATE Qt6::Test ${ARG_LIBS})
  target_include_directories(test_${name} PRIVATE ${UZUME_TESTUTIL_DIR})
  uzume_warnings(test_${name})
  add_test(NAME ${name} COMMAND test_${name})
  set(env "QT_QPA_PLATFORM=offscreen" "QT_QUICK_BACKEND=software")
  if(ARG_NEEDS_CORE)
    list(APPEND env "UZUME_MELONDS_DS_CORE=${UZUME_MELONDS_DS_CORE}")
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77)
  endif()
  if(ARG_NEEDS_DESMUME_CORE)
    list(APPEND env "UZUME_DESMUME_CORE=${UZUME_DESMUME_CORE}")
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77)
  endif()
  if(ARG_TEST_CORE)
    if(NOT ARG_TEST_CORE_FLAVOR)
      set(ARG_TEST_CORE_FLAVOR melondsds)
    endif()
    add_dependencies(test_${name} uzume_test_core)
    target_compile_definitions(test_${name} PRIVATE FB_TEST_CORE=1)
    if(ARG_TEST_CORE_FLAVOR STREQUAL "desmume")
      list(APPEND env "UZUME_DESMUME_CORE=$<TARGET_FILE:uzume_test_core>")
    else()
      # Only the test core is visible to the product code: no DeSmuME next to it, even if the shell exports one.
      list(APPEND env "UZUME_MELONDS_DS_CORE=$<TARGET_FILE:uzume_test_core>" "UZUME_DESMUME_CORE=")
    endif()
    list(APPEND env "FB_TEST_CORE_FLAVOR=${ARG_TEST_CORE_FLAVOR}" "UZUME_TEST_CORE=1")
  endif()
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "${env}")
endfunction()

# Follow-up packages add sources/tests only in their own subdirectory.
add_subdirectory(core)       # uzume_core (with network/)
add_subdirectory(network)
add_subdirectory(media)      # uzume_media_deps (libdatachannel, FFmpeg, Opus)
add_subdirectory(input)      # uzume_sdl3 (SDL3 gamepad only) + headless virtual-joystick test
add_subdirectory(emulation)  # uzume_emulation
add_subdirectory(plugins)    # uzume_plugins: plugin runtime (Lua sandbox, manager, host)
add_subdirectory(formats)    # uzume_formats: game-file format helpers for plugins (LZ, GARC, RomFS, textures, PK6)
add_subdirectory(ui)         # uzume_ui: QML module Uzume.App
add_subdirectory(app)        # uzume_app (executable from app/ + ui/)
add_subdirectory(launcher)   # uzume_launcher (Windows only): tiny top-level Uzume.exe; cmdline test everywhere
