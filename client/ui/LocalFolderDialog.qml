import QtQuick
import QtQuick.Dialogs

// Native folder dialog of "ROM folder" (LocalHubCard.qml loads it through a Loader and falls back to a path field when
// QtQuick.Dialogs is not installed). Own file so deployment tools see the import.
FolderDialog {
    id: dialog
    property var local: null
    title: qsTr("ROM folder of the Hub")
    onAccepted: if (dialog.local !== null) dialog.local.chooseFolder(dialog.selectedFolder.toString())
}
