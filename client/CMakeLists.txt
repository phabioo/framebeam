cmake_minimum_required(VERSION 3.25)
project(FrameBeamPlayer VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

function(framebeam_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra)
  endif()
endfunction()

add_library(framebeam_core STATIC core/version.cpp)
target_include_directories(framebeam_core PUBLIC core)
target_compile_features(framebeam_core PUBLIC cxx_std_20)
framebeam_warnings(framebeam_core)

if(BUILD_TESTING)
  add_executable(framebeam_core_tests tests/core_version_test.cpp)
  target_link_libraries(framebeam_core_tests PRIVATE framebeam_core)
  framebeam_warnings(framebeam_core_tests)
  add_test(NAME core_version COMMAND framebeam_core_tests)
endif()
