import QtQuick
import QtQuick.Dialogs

// Native save dialog of "Collect logs". Its own file with a real import so deployment tools (windeployqt --qmldir)
// see the QtQuick.Dialogs dependency; SettingsScreen loads it through a Loader and shows a note when the module is
// not installed.
FileDialog {
    id: dialog
    property var player: null
    title: qsTr("Save diagnostics bundle")
    fileMode: FileDialog.SaveFile
    defaultSuffix: "zip"
    nameFilters: [qsTr("Zip archive (*.zip)")]
    function prepare() {
        if (dialog.player === null) return
        const folder = dialog.player.diagnosticsFolderUrl()
        if (folder !== "") {
            dialog.currentFolder = folder
            dialog.currentFile = folder + "/" + dialog.player.diagnosticsFileName()
        }
    }
    onAccepted: if (dialog.player !== null) dialog.player.collectDiagnostics(dialog.selectedFile.toString())
}
