# framebeam_ui: QML-Modul FrameBeam.Player (statisch, wird in framebeam_player gelinkt).
# QML-Dateien, Komponenten und C++-Quellen der Oberflaeche hier ergaenzen.
qt_add_library(framebeam_ui STATIC)
qt_add_qml_module(framebeam_ui
  URI FrameBeam.Player
  VERSION 1.0
  RESOURCE_PREFIX /qt/qml
  QML_FILES Main.qml
)
target_link_libraries(framebeam_ui PUBLIC framebeam_core Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia)
framebeam_warnings(framebeam_ui)
