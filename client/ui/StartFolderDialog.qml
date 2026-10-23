import QtQuick
import QtQuick.Dialogs

// Native folder dialog of "Play on this PC" (StartPlayPage.qml loads it through a Loader and falls back to the path field when
// QtQuick.Dialogs is not installed). Own file so deployment tools see the import.
FolderDialog {
    id: dialog
    signal folderPicked(string url)
    title: qsTr("Pick the folder with your games")
    onAccepted: dialog.folderPicked(dialog.selectedFolder.toString())
}
