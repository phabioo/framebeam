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

    RowLayout {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.rightMargin: root.endMargin > 0 ? root.endMargin - 8 : 0
        Eyebrow {
            objectName: "libraryFoldersEyebrow"
            Layout.fillWidth: true
            text: qsTr("Library folders · %1").arg(root.folders.length)
        }
        FbButton {
            objectName: "libraryFolderAdd"
            kind: "link"
            text: qsTr("+ Add folder…")
            onClicked: root.openFolderDialog()
        }
    }
    FbLabel {
        objectName: "libraryFoldersHint"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.leftMargin: Theme.settingsDotSlot
        Layout.rightMargin: root.endMargin
        wrapMode: Text.WordWrap
        lineHeight: 1.35
        font.pixelSize: Theme.fontSmall
        color: Theme.textMuted
        text: qsTr("The Player finds games in these folders and starts them from there. It tells the Hub which games you have; the files stay on this device.")
    }

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
            implicitHeight: rowLayout.implicitHeight + 24
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderRow }
            RowLayout {
                id: rowLayout
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 24
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 2
                    FbMono {
                        objectName: "libraryFolderPath"
                        Layout.fillWidth: true
                        text: folderRow.modelData.path
                        font.pixelSize: Theme.fontSmall
                        color: Theme.text
                        elide: Text.ElideMiddle
                        HoverHandler { id: pathHover }
                        ToolTip.visible: pathHover.hovered && truncated
                        ToolTip.text: folderRow.modelData.path
                        ToolTip.delay: 400
                    }
                    FbLabel {
                        objectName: "libraryFolderStatus"
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.pixelSize: Theme.fontMeta
                        color: !folderRow.modelData.exists ? Theme.pluginWarn
                               : folderRow.modelData.scanning ? Theme.accent : Theme.textMuted
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
                    spacing: 6
                    FbButton {
                        objectName: folderRow.modelData.scanning ? "libraryFolderStop" : "libraryFolderRescan"
                        implicitHeight: 32
                        hPadding: 24
                        text: folderRow.modelData.scanning ? qsTr("Stop scan") : qsTr("Rescan")
                        enabled: folderRow.modelData.scanning || root.scan.active !== true
                        onClicked: folderRow.modelData.scanning ? root.player.stopLibraryScan() : root.player.rescanLibrary()
                    }
                    FbButton {
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
        implicitHeight: Math.max(52, emptyText.implicitHeight + 28)
        radius: Theme.radius10
        color: "transparent"
        border.width: 1
        border.color: Theme.borderButton
        FbLabel {
            id: emptyText
            anchors.centerIn: parent
            width: parent.width - 32
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("No folders yet. Add a folder that contains your game files.")
            font.pixelSize: Theme.fontSmall
            color: Theme.textFaint
        }
    }
    FbLabel {
        visible: root.folders.length > 0
        Layout.fillWidth: true
        Layout.leftMargin: Theme.settingsDotSlot
        Layout.rightMargin: root.endMargin
        wrapMode: Text.WordWrap
        font.pixelSize: Theme.fontMeta
        color: Theme.textMuted
        text: qsTr("Remove only takes the folder off the list. Your files are not deleted.")
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
        visible: root.scan.isError === true && (root.scan.text || "") !== ""
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.leftMargin: Theme.settingsDotSlot
        wrapMode: Text.WordWrap
        font.pixelSize: Theme.fontMeta
        color: Theme.errorText
        text: root.scan.text || ""
    }
}
