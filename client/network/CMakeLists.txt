# Netzwerk-Quellen des Kerns (Hub-Client, spaeter Signaling). Gehoert zum Ziel framebeam_core.
target_sources(framebeam_core PRIVATE
  # hier ergaenzen
)
target_include_directories(framebeam_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# framebeam_add_test(network_xyz tests/xyz_test.cpp LIBS framebeam_core)
