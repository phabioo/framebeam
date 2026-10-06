# framebeam_ui: QML module FrameBeam.Player (static, linked into framebeam_player).
# C++ glue (controller, models, GameView, audio) and QML screens of the UI.
set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_library(framebeam_ui STATIC)
qt_add_qml_module(framebeam_ui
  URI FrameBeam.Player
  VERSION 1.0
  RESOURCE_PREFIX /qt/qml
  OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/FrameBeam/Player
  SOURCES
    inputmapping.h inputmapping.cpp
    audiooutput.h audiooutput.cpp
    gamesession.h gamesession.cpp
    gameview.h gameview.cpp
    librarymodel.h librarymodel.cpp
    playercontroller.h playercontroller.cpp
    sessioncontroller.h sessioncontroller.cpp
    remoteview.h remoteview.cpp
  QML_FILES
    Main.qml
    Theme.qml
    FbLabel.qml
    FbMono.qml
    FbButton.qml
    FbToggle.qml
    FbField.qml
    Logo.qml
    StatusDot.qml
    Eyebrow.qml
    HubCard.qml
    StepCard.qml
    ConnectionScreen.qml
    PairingScreen.qml
    Sidebar.qml
    GameTile.qml
    DetailPane.qml
    LibraryScreen.qml
    GameScreen.qml
    SettingsScreen.qml
    ConflictDialog.qml
    FbSegment.qml
    SessionsSection.qml
    SessionPanel.qml
    MultiviewArea.qml
    DiagnosticsPanel.qml
)
target_include_directories(framebeam_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(framebeam_ui PUBLIC framebeam_core framebeam_audioutil framebeam_media framebeam_emulation Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia)
framebeam_warnings(framebeam_ui)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
