# UI tests (QtTest, offscreen). Source of the fake hub helper: network/tests (test code only).
set(FB_UI_TEST_LIBS framebeam_ui framebeam_uiplugin framebeam_core framebeam_emulation Qt6::Quick Qt6::QuickControls2 Qt6::QuickTest)
get_filename_component(FB_NETTEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../network/tests" ABSOLUTE)
get_filename_component(FB_ROM_DIR "${CMAKE_BINARY_DIR}/emulation" ABSOLUTE)

framebeam_add_test(ui_input input_test.cpp LIBS framebeam_ui framebeam_core framebeam_emulation)
framebeam_add_test(ui_audio audio_test.cpp LIBS framebeam_ui framebeam_core framebeam_emulation)

framebeam_add_test(ui_screens screens_test.cpp ${FB_NETTEST_DIR}/fakehub.cpp ${FB_NETTEST_DIR}/fakehub.h
  LIBS ${FB_UI_TEST_LIBS})
target_include_directories(test_ui_screens PRIVATE ${FB_NETTEST_DIR})
target_compile_definitions(test_ui_screens PRIVATE FB_TEST_DATA_DIR="${FB_NETTEST_DIR}/testdata")

# Game view with a real core and the homebrew test ROM (SKIP/77 without FRAMEBEAM_MELONDS_DS_CORE).
framebeam_add_test(ui_game game_test.cpp ${FB_NETTEST_DIR}/fakehub.cpp ${FB_NETTEST_DIR}/fakehub.h
  LIBS ${FB_UI_TEST_LIBS} NEEDS_CORE)
target_include_directories(test_ui_game PRIVATE ${FB_NETTEST_DIR})
target_compile_definitions(test_ui_game PRIVATE FB_TEST_DATA_DIR="${FB_NETTEST_DIR}/testdata"
  FB_TEST_ROM_PATH="${FB_ROM_DIR}/framebeam_test.nds")
add_dependencies(test_ui_game framebeam_test_rom)

# Save sync with a real core: conflict dialog (3d), decide later / use Hub version.
framebeam_add_test(ui_save save_ui_test.cpp ${FB_NETTEST_DIR}/fakehub.cpp ${FB_NETTEST_DIR}/fakehub.h
  LIBS ${FB_UI_TEST_LIBS} NEEDS_CORE)
target_include_directories(test_ui_save PRIVATE ${FB_NETTEST_DIR})
target_compile_definitions(test_ui_save PRIVATE FB_TEST_DATA_DIR="${FB_NETTEST_DIR}/testdata"
  FB_TEST_ROM_PATH="${FB_ROM_DIR}/framebeam_test.nds")
add_dependencies(test_ui_save framebeam_test_rom)

# sessions in the library, Session panel, multiview, diagnostics (FakeHub incl. WSS). Tests with a running
# game need the core (they QSKIP without it, the rest runs).
framebeam_add_test(ui_sessions sessions_ui_test.cpp ${FB_NETTEST_DIR}/fakehub.cpp ${FB_NETTEST_DIR}/fakehub.h
  LIBS ${FB_UI_TEST_LIBS} NEEDS_CORE)
target_include_directories(test_ui_sessions PRIVATE ${FB_NETTEST_DIR})
target_compile_definitions(test_ui_sessions PRIVATE FB_TEST_DATA_DIR="${FB_NETTEST_DIR}/testdata"
  FB_TEST_ROM_PATH="${FB_ROM_DIR}/framebeam_test.nds")
add_dependencies(test_ui_sessions framebeam_test_rom)

# invite redemption, user_disabled, upload, firmware state/materialization, core warnings, Settings (no core needed).
framebeam_add_test(ui_hub_features hub_features_ui_test.cpp ${FB_NETTEST_DIR}/fakehub.cpp ${FB_NETTEST_DIR}/fakehub.h
  LIBS ${FB_UI_TEST_LIBS})
target_include_directories(test_ui_hub_features PRIVATE ${FB_NETTEST_DIR})
target_compile_definitions(test_ui_hub_features PRIVATE FB_TEST_DATA_DIR="${FB_NETTEST_DIR}/testdata")

# Emulation page (3e, settings hierarchy, core options) and Controllers page (3f) with SDL virtual joysticks.
# The test with the real core QSKIPs without FRAMEBEAM_MELONDS_DS_CORE.
framebeam_add_test(ui_emulation_controllers emulation_controllers_test.cpp ${FB_NETTEST_DIR}/fakehub.cpp ${FB_NETTEST_DIR}/fakehub.h
  LIBS ${FB_UI_TEST_LIBS} framebeam_input framebeam_sdl3 NEEDS_CORE)
target_include_directories(test_ui_emulation_controllers PRIVATE ${FB_NETTEST_DIR})
target_compile_definitions(test_ui_emulation_controllers PRIVATE FB_TEST_DATA_DIR="${FB_NETTEST_DIR}/testdata")

# Controllers glyph sets, Button labels (Auto + override per device), drawn arrows, input test grid and layout at 1280 / 960
# (SDL virtual joysticks, no core needed).
framebeam_add_test(ui_controllers_glyphs controllers_glyphs_test.cpp ${FB_NETTEST_DIR}/fakehub.cpp ${FB_NETTEST_DIR}/fakehub.h
  LIBS ${FB_UI_TEST_LIBS} framebeam_input framebeam_sdl3)
target_include_directories(test_ui_controllers_glyphs PRIVATE ${FB_NETTEST_DIR})
target_compile_definitions(test_ui_controllers_glyphs PRIVATE FB_TEST_DATA_DIR="${FB_NETTEST_DIR}/testdata")
# Same test at devicePixelRatio 2 (icons are painted in device pixels; the arrows must stay equal).
add_test(NAME ui_controllers_glyphs_hidpi COMMAND test_ui_controllers_glyphs)
set_tests_properties(ui_controllers_glyphs_hidpi PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_SCALE_FACTOR=2")

# Save history, restore, snapshots, slot picker in the detail pane (no core needed).
framebeam_add_test(ui_save_history save_history_ui_test.cpp ${FB_NETTEST_DIR}/fakehub.cpp ${FB_NETTEST_DIR}/fakehub.h
  LIBS ${FB_UI_TEST_LIBS})
target_include_directories(test_ui_save_history PRIVATE ${FB_NETTEST_DIR})
target_compile_definitions(test_ui_save_history PRIVATE FB_TEST_DATA_DIR="${FB_NETTEST_DIR}/testdata")

# Screenshots of the out-of-game screens (design v4); skipped without FRAMEBEAM_SCREENSHOT_DIR.
framebeam_add_test(ui_screenshots_shell screenshots_shell_test.cpp ${FB_NETTEST_DIR}/fakehub.cpp ${FB_NETTEST_DIR}/fakehub.h
  LIBS ${FB_UI_TEST_LIBS})
target_include_directories(test_ui_screenshots_shell PRIVATE ${FB_NETTEST_DIR})
target_compile_definitions(test_ui_screenshots_shell PRIVATE FB_TEST_DATA_DIR="${FB_NETTEST_DIR}/testdata")

# Screen layouts (0.6 D12) and the diagnostics model (0.6 D5-D11): pure logic, no Qt Quick.
framebeam_add_test(ui_screenlayout screenlayout_test.cpp LIBS framebeam_ui framebeam_core framebeam_emulation)
framebeam_add_test(ui_diagnostics diagnostics_test.cpp LIBS framebeam_ui framebeam_core framebeam_emulation)

# Screenshots of the game view with fake data (0.6): QSKIP unless FRAMEBEAM_SCREENSHOT_DIR is set; no core needed.
framebeam_add_test(ui_screenshots_game screenshots_game_test.cpp ${FB_NETTEST_DIR}/fakehub.cpp ${FB_NETTEST_DIR}/fakehub.h
  LIBS ${FB_UI_TEST_LIBS})
target_include_directories(test_ui_screenshots_game PRIVATE ${FB_NETTEST_DIR})
target_compile_definitions(test_ui_screenshots_game PRIVATE FB_TEST_DATA_DIR="${FB_NETTEST_DIR}/testdata")

# Speed-up button, speed select, Space (preview game, no core).
framebeam_add_test(ui_fast_forward fast_forward_ui_test.cpp ${FB_NETTEST_DIR}/fakehub.cpp ${FB_NETTEST_DIR}/fakehub.h
  LIBS ${FB_UI_TEST_LIBS})
target_include_directories(test_ui_fast_forward PRIVATE ${FB_NETTEST_DIR})
target_compile_definitions(test_ui_fast_forward PRIVATE FB_TEST_DATA_DIR="${FB_NETTEST_DIR}/testdata")

# "← Library" keeps the game loaded in the background: strip, Resume/Quit, confirmation, input (preview game, no core).
framebeam_add_test(ui_background_game background_game_ui_test.cpp ${FB_NETTEST_DIR}/fakehub.cpp ${FB_NETTEST_DIR}/fakehub.h
  LIBS ${FB_UI_TEST_LIBS})
target_include_directories(test_ui_background_game PRIVATE ${FB_NETTEST_DIR})
target_compile_definitions(test_ui_background_game PRIVATE FB_TEST_DATA_DIR="${FB_NETTEST_DIR}/testdata")
