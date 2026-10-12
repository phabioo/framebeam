import QtQuick
import QtQuick.Dialogs

// Native file dialog of Settings > Plugins "Install from file…" (PluginsSection.qml loads it through a Loader and
// falls back to a path field when QtQuick.Dialogs is not installed). Accepts .fbplugin files.
FileDialog {
    id: dialog
    property var plugins: null
    title: qsTr("Plugin file to install")
    fileMode: FileDialog.OpenFile
    nameFilters: [qsTr("FrameBeam plugins (*.fbplugin)"), qsTr("All files (*)")]
    onAccepted: {
        if (dialog.plugins === null) return
        dialog.plugins.installFromPath(dialog.selectedFile.toString())
    }
}
