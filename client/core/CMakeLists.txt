# framebeam_core: Player-Kern ohne GUI (core/ + network/). Quellen hier und in ../network ergaenzen.
add_library(framebeam_core STATIC version.cpp)
target_include_directories(framebeam_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(framebeam_core PUBLIC cxx_std_20)
target_link_libraries(framebeam_core PUBLIC Qt6::Core Qt6::Network)
framebeam_warnings(framebeam_core)

framebeam_add_test(core_version tests/core_version_test.cpp LIBS framebeam_core)
