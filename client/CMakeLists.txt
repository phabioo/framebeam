cmake_minimum_required(VERSION 3.25)

# The product version X.Y.Z comes from the root VERSION file (shared with the Hub, see docs/roadmap.md 0.3).
file(STRINGS ${CMAKE_CURRENT_LIST_DIR}/../VERSION _fb_version_line LIMIT_COUNT 1)
string(STRIP "${_fb_version_line}" _fb_version_core)
if(NOT _fb_version_core MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "VERSION must contain X.Y.Z, found '${_fb_version_core}'")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/../VERSION)
project(FrameBeamPlayer VERSION ${_fb_version_core} LANGUAGES CXX)

# Build identity (CI sets it, see S1): full SemVer version, update channel (stable|beta|dev; "test" is an alias of beta), commit.
# Empty FRAMEBEAM_VERSION = "<VERSION>-dev".
set(FRAMEBEAM_VERSION "" CACHE STRING "Full Player version (SemVer); empty: <VERSION file>-dev")
set(FRAMEBEAM_CHANNEL "dev" CACHE STRING "Compiled default update channel: stable | beta | dev (test = alias of beta)")
set(FRAMEBEAM_COMMIT "" CACHE STRING "Commit SHA of the build")
if(FRAMEBEAM_VERSION STREQUAL "")
  set(FRAMEBEAM_EFFECTIVE_VERSION "${PROJECT_VERSION}-dev")
else()
  set(FRAMEBEAM_EFFECTIVE_VERSION "${FRAMEBEAM_VERSION}")
endif()
if(FRAMEBEAM_CHANNEL STREQUAL "test")
  set(FRAMEBEAM_CHANNEL "beta")
endif()
if(NOT FRAMEBEAM_CHANNEL MATCHES "^(stable|beta|dev)$")
  message(FATAL_ERROR "FRAMEBEAM_CHANNEL must be stable, beta or dev")
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

# Path to the libretro core melonDS DS (.so/.dll), see scripts/fetch-melonds-ds.sh.
# Empty: everything builds, tests with NEEDS_CORE are skipped by ctest (SKIP), not failed.
set(FRAMEBEAM_MELONDS_DS_CORE "" CACHE FILEPATH "Path to the melonDS DS core library (.so/.dll)")
set(FRAMEBEAM_DESMUME_CORE "" CACHE FILEPATH "Path to the DeSmuME core library (.so/.dll); empty = DeSmuME tests are skipped")

include(CTest)

function(framebeam_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra)
  endif()
endfunction()

# framebeam_add_test(<name> <sources...> [LIBS <libs...>] [NEEDS_CORE])
# QtTest/plain test as executable test_<name>, ctest name <name>, runs with QT_QPA_PLATFORM=offscreen.
# NEEDS_CORE: passes FRAMEBEAM_MELONDS_DS_CORE as an environment variable; if the core is not set,
# the test ends with return code 77 (= SKIP). The test itself must then return 77.
# NEEDS_DESMUME_CORE: same with FRAMEBEAM_DESMUME_CORE.
set(FRAMEBEAM_TESTUTIL_DIR ${CMAKE_CURRENT_LIST_DIR}/testutil)
function(framebeam_add_test name)
  cmake_parse_arguments(ARG "NEEDS_CORE;NEEDS_DESMUME_CORE" "" "LIBS" ${ARGN})
  if(NOT BUILD_TESTING)
    return()
  endif()
  add_executable(test_${name} ${ARG_UNPARSED_ARGUMENTS})
  target_link_libraries(test_${name} PRIVATE Qt6::Test ${ARG_LIBS})
  target_include_directories(test_${name} PRIVATE ${FRAMEBEAM_TESTUTIL_DIR})
  framebeam_warnings(test_${name})
  add_test(NAME ${name} COMMAND test_${name})
  set(env "QT_QPA_PLATFORM=offscreen" "QT_QUICK_BACKEND=software")
  if(ARG_NEEDS_CORE)
    list(APPEND env "FRAMEBEAM_MELONDS_DS_CORE=${FRAMEBEAM_MELONDS_DS_CORE}")
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77)
  endif()
  if(ARG_NEEDS_DESMUME_CORE)
    list(APPEND env "FRAMEBEAM_DESMUME_CORE=${FRAMEBEAM_DESMUME_CORE}")
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77)
  endif()
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "${env}")
endfunction()

# Follow-up packages add sources/tests only in their own subdirectory.
add_subdirectory(core)       # framebeam_core (with network/)
add_subdirectory(network)
add_subdirectory(media)      # framebeam_media_deps (libdatachannel, FFmpeg, Opus)
add_subdirectory(input)      # framebeam_sdl3 (SDL3 gamepad only) + headless virtual-joystick test
add_subdirectory(emulation)  # framebeam_emulation
add_subdirectory(ui)         # framebeam_ui: QML module FrameBeam.Player
add_subdirectory(app)        # framebeam_player (executable from app/ + ui/)
add_subdirectory(launcher)   # framebeam_player_launcher (Windows only): tiny top-level framebeam_player.exe; cmdline test everywhere
