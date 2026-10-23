import QtQuick
import QtQuick.Dialogs

// Native save dialog of the admin pages (server log, backups, report files). Its own file with a real import so deployment tools
// (windeployqt --qmldir) see the QtQuick.Dialogs dependency; pages load it through a Loader and show a note when the module is
// not installed. `prepare(fileName)` presets the folder (Desktop or Downloads) and the suggested name; `chosen(pathOrUrl)`
// carries the pick, which goes to `player.admin.download()`.
FileDialog {
    id: dialog
    property var player: null
    property string suffix: "txt"
    signal chosen(string pathOrUrl)
    title: qsTr("Save file")
    fileMode: FileDialog.SaveFile
    defaultSuffix: dialog.suffix
    function prepare(fileName: string) {
        if (dialog.player === null) return
        const folder = dialog.player.diagnosticsFolderUrl()
        if (folder !== "") {
            dialog.currentFolder = folder
            dialog.currentFile = folder + "/" + fileName
        }
    }
    onAccepted: dialog.chosen(dialog.selectedFile.toString())
}
