cmake_minimum_required(VERSION 3.25)
project(FrameBeamPlayer VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt >= 6.4 does NOT come from vcpkg: Linux apt (noble: 6.4.2), Windows install-qt-action (6.8 LTS).
# Code must work with the Qt 6.4 API.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Concurrent Network Gui Quick QuickControls2 QuickTest Multimedia Test)
qt_standard_project_setup()
set(CMAKE_AUTOMOC ON)

# Path to the libretro core melonDS DS (.so/.dll), see scripts/fetch-melonds-ds.sh.
# Empty: everything builds, tests with NEEDS_CORE are skipped by ctest (SKIP), not failed.
set(FRAMEBEAM_MELONDS_DS_CORE "" CACHE FILEPATH "Path to the melonDS DS core library (.so/.dll)")

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
function(framebeam_add_test name)
  cmake_parse_arguments(ARG "NEEDS_CORE" "" "LIBS" ${ARGN})
  if(NOT BUILD_TESTING)
    return()
  endif()
  add_executable(test_${name} ${ARG_UNPARSED_ARGUMENTS})
  target_link_libraries(test_${name} PRIVATE Qt6::Test ${ARG_LIBS})
  framebeam_warnings(test_${name})
  add_test(NAME ${name} COMMAND test_${name})
  set(env "QT_QPA_PLATFORM=offscreen" "QT_QUICK_BACKEND=software")
  if(ARG_NEEDS_CORE)
    list(APPEND env "FRAMEBEAM_MELONDS_DS_CORE=${FRAMEBEAM_MELONDS_DS_CORE}")
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77)
  endif()
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "${env}")
endfunction()

# Follow-up packages add sources/tests only in their own subdirectory.
add_subdirectory(core)       # framebeam_core (with network/)
add_subdirectory(network)
add_subdirectory(emulation)  # framebeam_emulation
add_subdirectory(ui)         # framebeam_ui: QML module FrameBeam.Player
add_subdirectory(app)        # framebeam_player (executable from app/ + ui/)
