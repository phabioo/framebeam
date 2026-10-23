import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Uzume.App

// Settings > Library folders (0.12, round 3 3p-3): folders scanned for game files on this device, one row each with its game
// count and last scan, "Rescan" (or "Stop scan" while it runs) and "Remove". Files are hashed once, read for their game code
// and registered with the Hub; they start from the folder without a copy in the ROM cache.
ColumnLayout {
    id: root
    required property PlayerController player
    readonly property var scan: player.libraryScan
    readonly property var folders: player.libraryFolderInfo
    property bool pathPromptOpen: false
    // Rows end where the controls of the SettingsRows end (the reset column of 72 stays empty on this page).
    readonly property real endMargin: width >= Theme.settingsNarrowBelow ? Theme.settingsResetWidth : 0
    spacing: Theme.space15

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

    RowLayout {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.rightMargin: root.endMargin > 0 ? root.endMargin - 8 : 0
        UzSectionLabel {
            objectName: "libraryFoldersEyebrow"
            Layout.fillWidth: true
            text: qsTr("Library folders · %1").arg(root.folders.length)
        }
        UzButton {
            objectName: "libraryFolderAdd"
            kind: "link"
            text: qsTr("+ Add folder…")
            onClicked: root.openFolderDialog()
        }
    }
    UzLabel {
        objectName: "libraryFoldersHint"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.leftMargin: Theme.settingsDotSlot
        Layout.rightMargin: root.endMargin
        wrapMode: Text.WordWrap
        lineHeight: Theme.roles.small.line
        lineHeightMode: Text.FixedHeight
        font.pixelSize: Theme.roles.input.size
        color: Theme.muted
        text: qsTr("Uzume finds games in these folders and starts them from there. It tells the server which games you have; the files stay on this device.")
    }

    Rectangle { Layout.fillWidth: true; Layout.rightMargin: root.endMargin; Layout.leftMargin: Theme.settingsDotSlot; height: 1; color: Theme.rule; Layout.topMargin: -Theme.space05 }

    Repeater {
        model: root.folders
        delegate: Item {
            id: folderRow
            required property var modelData
            objectName: "libraryFolderRow"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.leftMargin: Theme.settingsDotSlot
            Layout.rightMargin: root.endMargin
            implicitHeight: rowLayout.implicitHeight + Theme.space3
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule2 }
            RowLayout {
                id: rowLayout
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space3
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 2
                    UzMono {
                        objectName: "libraryFolderPath"
                        Layout.fillWidth: true
                        text: folderRow.modelData.path
                        font.pixelSize: Theme.roles.input.size
                        color: Theme.ink
                        elide: Text.ElideMiddle
                        HoverHandler { id: pathHover }
                        ToolTip.visible: pathHover.hovered && truncated
                        ToolTip.text: folderRow.modelData.path
                        ToolTip.delay: 400
                    }
                    UzLabel {
                        objectName: "libraryFolderStatus"
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.pixelSize: Theme.roles.small.size
                        color: !folderRow.modelData.exists ? Theme.accentText
                               : folderRow.modelData.scanning ? Theme.accent : Theme.muted
                        text: !folderRow.modelData.exists
                              ? (folderRow.modelData.scannedText !== "" ? qsTr("Drive not found · last scanned %1").arg(folderRow.modelData.scannedText)
                                                                        : qsTr("Folder not found"))
                              : folderRow.modelData.scanning
                                ? (root.scan.total > 0 ? qsTr("Scanning · %1 of %2 files").arg(root.scan.done).arg(root.scan.total) : qsTr("Scanning…"))
                                : (folderRow.modelData.scannedText !== ""
                                   ? qsTr("%1 · scanned %2").arg(Theme.plural(folderRow.modelData.count, qsTr("game"), qsTr("games"))).arg(folderRow.modelData.scannedText)
                                   : qsTr("%1 · not scanned yet").arg(Theme.plural(folderRow.modelData.count, qsTr("game"), qsTr("games"))))
                    }
                }
                RowLayout {
                    Layout.alignment: Qt.AlignVCenter
                    spacing: Theme.space1
                    UzButton {
                        objectName: folderRow.modelData.scanning ? "libraryFolderStop" : "libraryFolderRescan"
                        implicitHeight: 32
                        hPadding: 24
                        text: folderRow.modelData.scanning ? qsTr("Stop scan") : qsTr("Rescan")
                        enabled: folderRow.modelData.scanning || root.scan.active !== true
                        onClicked: folderRow.modelData.scanning ? root.player.stopLibraryScan() : root.player.rescanLibrary()
                    }
                    UzButton {
                        objectName: "libraryFolderRemove"
                        implicitHeight: 32
                        hPadding: 24
                        text: qsTr("Remove")
                        onClicked: root.player.removeLibraryFolder(folderRow.modelData.path)
                    }
                }
            }
        }
    }
    // Empty: a dashed row instead of nothing (sheet 0-2 "Add an item to a list").
    Rectangle {
        objectName: "libraryFoldersEmpty"
        visible: root.folders.length === 0
        Layout.fillWidth: true
        Layout.leftMargin: Theme.settingsDotSlot
        Layout.rightMargin: root.endMargin
        implicitHeight: Math.max(Theme.row48, emptyText.implicitHeight + 2 * Theme.space2)
        color: "transparent"
        border.width: 1
        border.color: Theme.rule2
        UzLabel {
            id: emptyText
            anchors.centerIn: parent
            width: parent.width - 2 * Theme.space2
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("No folders yet. Add a folder that contains your game files.")
            font.pixelSize: Theme.roles.input.size
            color: Theme.dim
        }
    }
    UzLabel {
        visible: root.folders.length > 0
        Layout.fillWidth: true
        Layout.leftMargin: Theme.settingsDotSlot
        Layout.rightMargin: root.endMargin
        wrapMode: Text.WordWrap
        font.pixelSize: Theme.roles.small.size
        color: Theme.muted
        text: qsTr("Remove only takes the folder off the list. Your files are not deleted.")
    }

    RowLayout {
        objectName: "libraryFolderPathPrompt"
        visible: root.pathPromptOpen
        Layout.fillWidth: true
        spacing: Theme.space15
        UzField {
            id: pathField
            objectName: "libraryFolderPathField"
            Layout.fillWidth: true
            placeholderText: qsTr("Folder path, e.g. D:\\Games\\ROMs")
            onAccepted: applyPath.clicked()
        }
        UzButton {
            id: applyPath
            objectName: "libraryFolderApplyButton"
            implicitHeight: 32
            text: qsTr("Add")
            enabled: pathField.text.trim() !== ""
            onClicked: { root.player.addLibraryFolder(pathField.text.trim()); pathField.text = ""; root.pathPromptOpen = false }
        }
    }

    UzLabel {
        objectName: "libraryScanStatus"
        visible: root.scan.isError === true && (root.scan.text || "") !== ""
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.leftMargin: Theme.settingsDotSlot
        wrapMode: Text.WordWrap
        font.pixelSize: Theme.roles.small.size
        color: Theme.danger
        text: root.scan.text || ""
    }
}
