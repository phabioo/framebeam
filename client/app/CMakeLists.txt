# framebeam_player: Executable. The QML module and UI sources come from ../ui (framebeam_ui).
qt_add_executable(framebeam_player main.cpp)
target_link_libraries(framebeam_player PRIVATE
  framebeam_core framebeam_emulation framebeam_ui framebeam_uiplugin
  Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia)
framebeam_warnings(framebeam_player)
if(WIN32)
  # GUI subsystem: no console window on start. Qt::EntryPoint maps main() to WinMain. Console output of
  # --version/--help/--version-json goes through printToStdout() in main.cpp (inherited handle or parent console).
  set_target_properties(framebeam_player PROPERTIES WIN32_EXECUTABLE ON)
  target_link_libraries(framebeam_player PRIVATE shell32)
endif()

if(BUILD_TESTING)
  add_test(NAME app_smoke COMMAND framebeam_player --smoke-test)
  set_tests_properties(app_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    TIMEOUT 60)
endif()
