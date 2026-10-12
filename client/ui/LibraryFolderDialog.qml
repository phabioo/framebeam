import QtQuick
import QtQuick.Dialogs

// Native folder dialog of "Library folders" (LibraryFoldersSection.qml loads it through a Loader and falls back to a
// path field when QtQuick.Dialogs is not installed). Own file so deployment tools see the import.
FolderDialog {
    id: dialog
    property var player: null
    title: qsTr("Add library folder")
    onAccepted: if (dialog.player !== null) dialog.player.addLibraryFolder(dialog.selectedFolder.toString())
}
