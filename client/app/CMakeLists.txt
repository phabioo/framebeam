# framebeam_player: Executable. The QML module and UI sources come from ../ui (framebeam_ui).
qt_add_executable(framebeam_player main.cpp)
target_link_libraries(framebeam_player PRIVATE
  framebeam_core framebeam_emulation framebeam_ui framebeam_uiplugin
  Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia)
framebeam_warnings(framebeam_player)

if(BUILD_TESTING)
  add_test(NAME app_smoke COMMAND framebeam_player --smoke-test)
  set_tests_properties(app_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    TIMEOUT 60)
endif()
