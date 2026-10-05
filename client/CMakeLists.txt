cmake_minimum_required(VERSION 3.25)
project(FrameBeamPlayer VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt >= 6.4 kommt NICHT aus vcpkg: Linux apt (noble: 6.4.2), Windows install-qt-action (6.8 LTS).
# Code muss mit der Qt-6.4-API auskommen.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Concurrent Network Gui Quick QuickControls2 Multimedia Test)
qt_standard_project_setup()
set(CMAKE_AUTOMOC ON)

# Pfad zum libretro-Core melonDS DS (.so/.dll), siehe scripts/fetch-melonds-ds.sh.
# Leer: alles baut, Tests mit NEEDS_CORE werden von ctest uebersprungen (SKIP), nicht rot.
set(FRAMEBEAM_MELONDS_DS_CORE "" CACHE FILEPATH "Pfad zur melonDS-DS-Core-Bibliothek (.so/.dll)")

include(CTest)

function(framebeam_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra)
  endif()
endfunction()

# framebeam_add_test(<name> <sources...> [LIBS <libs...>] [NEEDS_CORE])
# QtTest-/Plain-Test als Executable test_<name>, ctest-Name <name>, laeuft mit QT_QPA_PLATFORM=offscreen.
# NEEDS_CORE: gibt FRAMEBEAM_MELONDS_DS_CORE als Umgebungsvariable mit; ist der Core nicht gesetzt,
# endet der Test mit Rueckgabecode 77 (= SKIP). Der Test selbst muss dann 77 zurueckgeben.
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

# Folgepakete ergaenzen Quellen/Tests nur im jeweiligen Unterverzeichnis.
add_subdirectory(core)       # framebeam_core (mit network/)
add_subdirectory(network)
add_subdirectory(emulation)  # framebeam_emulation
add_subdirectory(ui)         # framebeam_ui: QML-Modul FrameBeam.Player
add_subdirectory(app)        # framebeam_player (Executable aus app/ + ui/)
