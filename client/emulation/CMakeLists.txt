# framebeam_emulation: EmulatorBackend/LibretroBackend (Core spaeter per dlopen/LoadLibrary, kein Link).
add_library(framebeam_emulation STATIC emulation.cpp)
target_include_directories(framebeam_emulation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(framebeam_emulation PUBLIC cxx_std_20)
target_link_libraries(framebeam_emulation PUBLIC Qt6::Core ${CMAKE_DL_LIBS})
framebeam_warnings(framebeam_emulation)

# Tests mit Core: framebeam_add_test(emulation_xyz tests/xyz_test.cpp LIBS framebeam_emulation NEEDS_CORE)
