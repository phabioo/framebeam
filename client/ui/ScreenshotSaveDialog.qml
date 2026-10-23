import QtQuick
import QtQuick.Dialogs

// Native save dialog of "Save as..." in the screenshot viewer. Its own file with a real import so deployment tools
// (windeployqt --qmldir) see the QtQuick.Dialogs dependency; ScreenshotViewer loads it through a Loader and shows a note when the
// module is not installed.
FileDialog {
    id: dialog
    property var shots: null
    property string screenshotId: ""
    title: qsTr("Save screenshot")
    fileMode: FileDialog.SaveFile
    defaultSuffix: "png"
    nameFilters: [qsTr("PNG image (*.png)")]
    function prepare() {
        if (dialog.shots === null) return
        const folder = dialog.shots.exportFolderUrl()
        if (folder !== "") {
            dialog.currentFolder = folder
            dialog.currentFile = folder + "/" + dialog.shots.suggestedFileName(dialog.screenshotId)
        }
    }
    onAccepted: if (dialog.shots !== null) dialog.shots.saveAs(dialog.screenshotId, dialog.selectedFile.toString())
}
