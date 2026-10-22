import QtQuick
import QtQuick.Dialogs
import FrameBeam.Player

// Native save dialog of "Report a problem" -> "Save as file…". Its own file with a real import so deployment tools
// (windeployqt --qmldir) see the QtQuick.Dialogs dependency; ReportProblemDialog loads it through a Loader.
FileDialog {
    id: dialog
    property ReportController ctl: null
    title: qsTr("Save problem report")
    fileMode: FileDialog.SaveFile
    defaultSuffix: "zip"
    nameFilters: [qsTr("Zip archive (*.zip)")]
    function prepare() {
        if (dialog.ctl === null) return
        const folder = dialog.ctl.folderUrl()
        if (folder !== "") {
            dialog.currentFolder = folder
            dialog.currentFile = folder + "/" + dialog.ctl.suggestedFileName()
        }
    }
    onAccepted: if (dialog.ctl !== null) dialog.ctl.saveAsFile(dialog.selectedFile.toString())
}
