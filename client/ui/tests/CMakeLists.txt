# UI-Tests (QtTest, offscreen). Quelle der Fake-Hub-Hilfe: network/tests (nur Testcode).
set(FB_UI_TEST_LIBS framebeam_ui framebeam_uiplugin framebeam_core framebeam_emulation Qt6::Quick Qt6::QuickControls2)
get_filename_component(FB_NETTEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../network/tests" ABSOLUTE)
get_filename_component(FB_ROM_DIR "${CMAKE_BINARY_DIR}/emulation" ABSOLUTE)

framebeam_add_test(ui_input input_test.cpp LIBS framebeam_ui framebeam_core framebeam_emulation)
framebeam_add_test(ui_audio audio_test.cpp LIBS framebeam_ui framebeam_core framebeam_emulation)

framebeam_add_test(ui_screens screens_test.cpp ${FB_NETTEST_DIR}/fakehub.cpp ${FB_NETTEST_DIR}/fakehub.h
  LIBS ${FB_UI_TEST_LIBS})
target_include_directories(test_ui_screens PRIVATE ${FB_NETTEST_DIR})
target_compile_definitions(test_ui_screens PRIVATE FB_TEST_DATA_DIR="${FB_NETTEST_DIR}/testdata")

# Spielansicht mit echtem Core und der Homebrew-Test-ROM (SKIP/77 ohne FRAMEBEAM_MELONDS_DS_CORE).
framebeam_add_test(ui_game game_test.cpp ${FB_NETTEST_DIR}/fakehub.cpp ${FB_NETTEST_DIR}/fakehub.h
  LIBS ${FB_UI_TEST_LIBS} NEEDS_CORE)
target_include_directories(test_ui_game PRIVATE ${FB_NETTEST_DIR})
target_compile_definitions(test_ui_game PRIVATE FB_TEST_DATA_DIR="${FB_NETTEST_DIR}/testdata"
  FB_TEST_ROM_PATH="${FB_ROM_DIR}/framebeam_test.nds")
add_dependencies(test_ui_game framebeam_test_rom)
