# Network sources of the core (hub client, signaling later). Belongs to the framebeam_core target.
target_sources(framebeam_core PRIVATE
  hubprotocol.cpp
  hubhttp.cpp
  hubconnection.cpp
  hublibrary.cpp
  romdownloader.cpp
  saveapi.cpp
  savesync.cpp
  sessiontypes.cpp
  sessionapi.cpp
  hubsocket.cpp
  hubsystems.cpp
  firmwareprovisioner.cpp
  coreprovisioner.cpp
  corechoice.cpp
  gameuploader.cpp
)
target_link_libraries(framebeam_core PUBLIC Qt6::WebSockets)
target_include_directories(framebeam_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Developer tool against a real hub (pairing, library, saves, Sessions: session-share / session-watch).
add_executable(framebeam_player_cli cli/main.cpp cli/sessioncommands.cpp cli/sessioncommands.h)
target_link_libraries(framebeam_player_cli PRIVATE framebeam_core framebeam_media)
framebeam_warnings(framebeam_player_cli)

set(FB_TESTDATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/testdata")
set(FB_FAKEHUB tests/fakehub.cpp tests/fakehub.h)

framebeam_add_test(network_hub_flow tests/hub_flow_test.cpp ${FB_FAKEHUB} LIBS framebeam_core)
framebeam_add_test(network_library_rom tests/library_rom_test.cpp ${FB_FAKEHUB} LIBS framebeam_core)
framebeam_add_test(network_savesync tests/savesync_test.cpp ${FB_FAKEHUB} LIBS framebeam_core)
framebeam_add_test(network_sessions tests/sessions_test.cpp ${FB_FAKEHUB} LIBS framebeam_core)
framebeam_add_test(network_hub_features tests/hub_features_test.cpp ${FB_FAKEHUB} LIBS framebeam_core)
framebeam_add_test(network_coreprovisioner tests/coreprovisioner_test.cpp ${FB_FAKEHUB} LIBS framebeam_core)
framebeam_add_test(network_corechoice tests/corechoice_test.cpp LIBS framebeam_core)
if(BUILD_TESTING)
  foreach(t network_hub_flow network_library_rom network_savesync network_sessions network_hub_features network_coreprovisioner)
    target_compile_definitions(test_${t} PRIVATE FB_TEST_DATA_DIR="${FB_TESTDATA_DIR}")
  endforeach()
endif()
