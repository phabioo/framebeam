import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Settings > Library folders (0.12): folders scanned for game files on this device. Files are hashed once, read for
// their game code and registered with the Hub; they start from the folder without a copy in the ROM cache.
ColumnLayout {
    id: root
    required property PlayerController player
    readonly property var scan: player.libraryScan
    property bool pathPromptOpen: false
    spacing: Theme.space12

    Loader {
        id: folderDialogLoader
        active: false
        source: "LibraryFolderDialog.qml"
        onLoaded: item.player = root.player
    }
    function openFolderDialog() {
        folderDialogLoader.active = true
        if (folderDialogLoader.status === Loader.Ready && folderDialogLoader.item !== null) {
            folderDialogLoader.item.open()
        } else {
            root.pathPromptOpen = !root.pathPromptOpen
        }
    }

    Eyebrow { text: qsTr("Library folders") }
    FbLabel {
        objectName: "libraryFoldersHint"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        wrapMode: Text.WordWrap
        font.pixelSize: Theme.fontMeta
        color: Theme.textMuted
        text: qsTr("Folders on this device with your game files. The Player finds games in them, tells the Hub which games you have (the files stay here) and starts them directly from the folder.")
    }

    Repeater {
        model: root.player.libraryFolders
        delegate: RowLayout {
            id: folderRow
            required property string modelData
            objectName: "libraryFolderRow"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: Theme.space10
            FbMono {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: folderRow.modelData
                elide: Text.ElideMiddle
            }
            FbButton {
                objectName: "libraryFolderRemove"
                implicitHeight: 32
                text: qsTr("Remove")
                onClicked: root.player.removeLibraryFolder(folderRow.modelData)
            }
        }
    }
    FbLabel {
        objectName: "libraryFoldersEmpty"
        visible: root.player.libraryFolders.length === 0
        Layout.fillWidth: true
        font.pixelSize: Theme.fontMeta
        color: Theme.textFaint
        text: qsTr("No folders yet.")
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space10
        FbButton {
            objectName: "libraryFolderAdd"
            implicitHeight: 32
            text: qsTr("Add folder…")
            onClicked: root.openFolderDialog()
        }
        FbButton {
            objectName: "libraryFolderRescan"
            implicitHeight: 32
            text: qsTr("Rescan")
            enabled: root.player.libraryFolders.length > 0 && root.scan.active !== true
            onClicked: root.player.rescanLibrary()
        }
    }
    RowLayout {
        objectName: "libraryFolderPathPrompt"
        visible: root.pathPromptOpen
        Layout.fillWidth: true
        spacing: Theme.space10
        FbField {
            id: pathField
            objectName: "libraryFolderPathField"
            Layout.fillWidth: true
            placeholderText: qsTr("Folder path, e.g. D:\\Games\\ROMs")
            onAccepted: applyPath.clicked()
        }
        FbButton {
            id: applyPath
            objectName: "libraryFolderApplyButton"
            implicitHeight: 32
            text: qsTr("Add")
            enabled: pathField.text.trim() !== ""
            onClicked: { root.player.addLibraryFolder(pathField.text.trim()); pathField.text = ""; root.pathPromptOpen = false }
        }
    }

    FbLabel {
        objectName: "libraryScanStatus"
        visible: (root.scan.text || "") !== ""
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        wrapMode: Text.WordWrap
        font.pixelSize: Theme.fontMeta
        color: root.scan.isError === true ? Theme.errorText : Theme.textMuted
        text: root.scan.active === true && root.scan.total > 0
              ? qsTr("Scanning… %1 of %2 files").arg(root.scan.done).arg(root.scan.total)
              : (root.scan.text || "")
    }
    Rectangle {
        objectName: "libraryScanProgress"
        visible: root.scan.active === true && root.scan.total > 0
        Layout.fillWidth: true
        Layout.preferredHeight: 4
        radius: 2
        color: Theme.surfaceRaised
        Rectangle {
            height: parent.height
            radius: 2
            width: parent.width * (root.scan.total > 0 ? root.scan.done / root.scan.total : 0)
            color: Theme.accent
        }
    }
}
