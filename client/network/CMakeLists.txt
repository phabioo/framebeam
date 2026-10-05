# Network sources of the core (hub client, signaling later). Belongs to the framebeam_core target.
target_sources(framebeam_core PRIVATE
  hubprotocol.cpp
  hubhttp.cpp
  hubconnection.cpp
  hublibrary.cpp
  romdownloader.cpp
)
target_include_directories(framebeam_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Developer tool against a real hub (only QtCore/QtNetwork via framebeam_core).
add_executable(framebeam_player_cli cli/main.cpp)
target_link_libraries(framebeam_player_cli PRIVATE framebeam_core)
framebeam_warnings(framebeam_player_cli)

set(FB_TESTDATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/testdata")
set(FB_FAKEHUB tests/fakehub.cpp tests/fakehub.h)

framebeam_add_test(network_hub_flow tests/hub_flow_test.cpp ${FB_FAKEHUB} LIBS framebeam_core)
framebeam_add_test(network_library_rom tests/library_rom_test.cpp ${FB_FAKEHUB} LIBS framebeam_core)
if(BUILD_TESTING)
  foreach(t network_hub_flow network_library_rom)
    target_compile_definitions(test_${t} PRIVATE FB_TEST_DATA_DIR="${FB_TESTDATA_DIR}")
  endforeach()
endif()
