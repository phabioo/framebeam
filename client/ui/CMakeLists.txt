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
    timeformat.h timeformat.cpp
    screenlayout.h screenlayout.cpp
    diagnosticsmodel.h diagnosticsmodel.cpp
    emulationcontroller.h emulationcontroller.cpp
    controllerscontroller.h controllerscontroller.cpp
    controllersglyphs.h controllersglyphs.cpp
    audiooutput.h audiooutput.cpp
    gamesession.h gamesession.cpp
    gpuencodebridge.h gpuencodebridge.cpp
    gpudisplaybridge.h gpudisplaybridge.cpp
    gameview.h gameview.cpp
    librarymodel.h librarymodel.cpp
    playercontroller.h playercontroller.cpp
    playphase.h
    corecatalog.h corecatalog.cpp
    coreprober.h coreprober.cpp
    gamedetail.h gamedetail.cpp
    hubpresenter.h hubpresenter.cpp
    gamestarter.h gamestarter.cpp
    sessioncontroller.h sessioncontroller.cpp
    remoteview.h remoteview.cpp
    updatescontroller.h updatescontroller.cpp
    savehistorycontroller.h savehistorycontroller.cpp
    localhubcontroller.h localhubcontroller.cpp
    pluginruntime.h pluginruntime.cpp
    pluginscontroller.h pluginscontroller.cpp
    keyedrowsmodel.h keyedrowsmodel.cpp
    runscontroller.h runscontroller.cpp
    pluginimageprovider.h pluginimageprovider.cpp
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
    MoreMenu.qml
    StepCard.qml
    ConnectionScreen.qml
    PairingScreen.qml
    Sidebar.qml
    GameTile.qml
    DetailPane.qml
    LibraryScreen.qml
    GameGrid.qml
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
    TileStateCard.qml
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
    CollectLogsDialog.qml
    LocalHubForm.qml
    LocalHubCard.qml
    LocalFolderDialog.qml
    LibraryFolderDialog.qml
    LibraryFoldersSection.qml
    PluginFolderDialog.qml
    PluginFileDialog.qml
    PluginOriginPill.qml
    PluginsSection.qml
    RunsScreen.qml
    RunAvatars.qml
    RunCheckRow.qml
    UnverifiedNote.qml
    GamePickList.qml
    RunJoinDialog.qml
    RunStartDialog.qml
    PluginConsole.qml
    PluginFocusable.qml
    PluginImage.qml
    PluginConfirmDialog.qml
    PluginButton.qml
    PluginToggle.qml
    PluginSegmented.qml
    PluginSelect.qml
    PluginTextField.qml
    PluginList.qml
    PluginTable.qml
    PluginMapView.qml
    PluginChild.qml
    PluginBlock.qml
    PluginDetailSection.qml
    PluginDetailSheet.qml
    PluginRunSection.qml
    PluginTabIcon.qml
    PluginMenu.qml
    PluginPanelContent.qml
    PluginScreenSheet.qml
    PluginHudWidget.qml
    PluginHudStack.qml
    PluginHudLayer.qml
    PluginToast.qml
    PluginTileStrip.qml
    PluginScreen.qml
)
# Zero-copy display (ADR 0027): the platform implementation of makePlatformDisplay(); Windows has its own (WIN32 block below).
if(NOT WIN32)
  target_sources(framebeam_ui PRIVATE gpudisplay_platform_stub.cpp)
endif()
target_include_directories(framebeam_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(framebeam_ui PUBLIC framebeam_core framebeam_input framebeam_audioutil framebeam_media framebeam_emulation framebeam_plugins framebeam_formats Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia)
framebeam_warnings(framebeam_ui)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

# Zero-copy display (ADR 0027), Windows platform factory and D3D11 texture source.
if(WIN32)
  target_sources(framebeam_ui PRIVATE gpudisplay_platform_win.cpp)
  target_link_libraries(framebeam_ui PRIVATE d3d11 dxgi)
endif()
