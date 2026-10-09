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
    screenlayout.h screenlayout.cpp
    diagnosticsmodel.h diagnosticsmodel.cpp
    emulationcontroller.h emulationcontroller.cpp
    controllerscontroller.h controllerscontroller.cpp
    controllersglyphs.h controllersglyphs.cpp
    audiooutput.h audiooutput.cpp
    gamesession.h gamesession.cpp
    gpuencodebridge.h gpuencodebridge.cpp
    gameview.h gameview.cpp
    librarymodel.h librarymodel.cpp
    playercontroller.h playercontroller.cpp
    playphase.h
    corecatalog.h corecatalog.cpp
    gamedetail.h gamedetail.cpp
    hubpresenter.h hubpresenter.cpp
    gamestarter.h gamestarter.cpp
    sessioncontroller.h sessioncontroller.cpp
    remoteview.h remoteview.cpp
    updatescontroller.h updatescontroller.cpp
    savehistorycontroller.h savehistorycontroller.cpp
  QML_FILES
    Main.qml
    Theme.qml
    FbLabel.qml
    FbMono.qml
    FbButton.qml
    FbToggle.qml
    FbField.qml
    FbPill.qml
    FbChip.qml
    FbSpinner.qml
    ShellColumn.qml
    ColumnItem.qml
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
    StartConfirmDialog.qml
    FbSegment.qml
    SessionsSection.qml
    GamePanel.qml
    GameHeader.qml
    GameButton.qml
    GameIcon.qml
    GameSegment.qml
    SharePill.qml
    AnchoredPopover.qml
    PopoverItem.qml
    SessionPicker.qml
    MultiviewArea.qml
    Sparkline.qml
    DiagSection.qml
    DiagRow.qml
    DiagParticipants.qml
    DiagFooter.qml
    DiagnosticsOverlay.qml
    LayoutSwitch.qml
    KeyHintButton.qml
    FbSelect.qml
    SettingsRow.qml
    EmulationScreen.qml
    ControllersScreen.qml
    PadGlyph.qml
    UploadFileDialog.qml
    SaveSummary.qml
    SavesView.qml
    SaveRow.qml
    SortControl.qml
    SaveFileDialog.qml
)
target_include_directories(framebeam_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(framebeam_ui PUBLIC framebeam_core framebeam_input framebeam_audioutil framebeam_media framebeam_emulation Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia)
framebeam_warnings(framebeam_ui)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
