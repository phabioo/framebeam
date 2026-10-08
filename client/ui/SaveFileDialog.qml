import QtQuick
import QtQuick.Dialogs

// Native file dialog of "Upload save file...". Its own file with a real import so deployment tools
// (windeployqt --qmldir) see the QtQuick.Dialogs dependency; SaveHistorySection loads it through a Loader and
// falls back to a path field when the module is not installed.
FileDialog {
    id: dialog
    property var history: null
    title: qsTr("Upload save file")
    fileMode: FileDialog.OpenFile
    nameFilters: dialog.history !== null ? dialog.history.saveFileFilters : []
    onAccepted: if (dialog.history !== null) dialog.history.requestUploadFile(dialog.selectedFile.toString())
}
