import QtQuick
import QtQuick.Dialogs

// Native "open file" dialog of the admin pages (a firmware file, a plugin file). Its own file with a real import so deployment tools
// (windeployqt --qmldir) see the QtQuick.Dialogs dependency; pages load it through a Loader and show a note when the module is not
// installed. `chosen(fileUrl)` carries the pick, which goes to `player.admin.upload()`.
FileDialog {
    id: dialog
    signal chosen(string fileUrl)
    title: qsTr("Choose a file")
    fileMode: FileDialog.OpenFile
    onAccepted: dialog.chosen(dialog.selectedFile.toString())
}
