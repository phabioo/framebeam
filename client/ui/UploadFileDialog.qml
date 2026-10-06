import QtQuick
import QtQuick.Dialogs

// Native file dialog of "Upload ROM". Its own file with a real import so deployment tools (windeployqt --qmldir)
// see the QtQuick.Dialogs dependency; LibraryScreen loads it through a Loader and falls back to a path field
// when the module is not installed.
FileDialog {
    id: dialog
    property var player: null
    title: qsTr("Upload ROM")
    fileMode: FileDialog.OpenFile
    nameFilters: dialog.player !== null ? dialog.player.uploadFilters : []
    onAccepted: if (dialog.player !== null) dialog.player.uploadRom(dialog.selectedFile.toString())
}
