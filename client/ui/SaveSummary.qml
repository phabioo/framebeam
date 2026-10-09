import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Compact SAVE block of the detail column (3c-2): slot, current version, meta, sync pill and the slot/version counts.
// "Manage saves →" ("View saves →" offline) opens the saves view. States: normal, sync pending, Hub offline,
// save conflict, no saves yet, Hub without save sync.
Rectangle {
    id: root
    required property PlayerController player
    readonly property SaveHistoryController hist: player.saveHistory
    readonly property var game: player.selectedGame
    readonly property string syncKind: game.syncKind || "none"
    readonly property bool conflict: syncKind === "conflict"
    readonly property bool offline: hist.slotsAvailable ? !hist.online : hist.hasSaveData
    readonly property bool unsupported: !hist.hasSaveData
    readonly property bool noSaves: hist.slotsAvailable && hist.online && !hist.loading && hist.current.revision === undefined
                                    && hist.history.length === 0 && (syncKind === "none" || hist.available)
    signal manage()
    signal uploadRequested()

    objectName: "saveSummary"
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    Layout.preferredWidth: 1
    implicitHeight: col.implicitHeight + 24
    radius: Theme.radius9
    color: Theme.surface
    border.width: 1
    border.color: Theme.borderCard

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.topMargin: 12
        anchors.bottomMargin: 12
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 10

        // Header: eyebrow + link
        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            implicitHeight: 24
            Eyebrow {
                anchors.left: parent.left
                anchors.right: link.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: root.conflict ? qsTr("▲ Save conflict") : qsTr("Save")
                color: root.conflict ? Theme.warn : Theme.textFaint
            }
            FbButton {
                id: link
                objectName: "manageSavesLink"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: !root.unsupported
                kind: "link"
                implicitHeight: 24
                font.pixelSize: Theme.fontSmall
                font.weight: Font.Medium
                text: root.noSaves ? (root.hist.canUploadFile ? qsTr("Upload save file…") : qsTr("Manage saves →"))
                      : root.offline ? qsTr("View saves →") : qsTr("Manage saves →")
                onClicked: root.noSaves && root.hist.canUploadFile ? root.uploadRequested() : root.manage()
            }
        }

        // Hub without save sync (or user unknown): the old "Save" row text
        FbLabel {
            objectName: "saveUnsupported"
            visible: root.unsupported
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: root.game.saveText || ""
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
        }

        // Conflict
        ColumnLayout {
            objectName: "saveConflictBlock"
            visible: root.conflict && !root.unsupported
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 8
            FbLabel {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("The hub and this device have different saves for “%1”. Nothing is overwritten until you decide.").arg(root.hist.slotLabel)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
            }
            FbButton {
                objectName: "resolveConflictButton"
                implicitHeight: 32
                font.pixelSize: Theme.fontSmall
                text: qsTr("Resolve conflict")
                enabled: root.game.canPlay === true && !root.hist.confirmationOpen
                onClicked: root.player.playSelected()
            }
            FbLabel {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("The conflict dialog opens when the game starts. Restore, upload and new slot are paused for “%1” until it is resolved.").arg(root.hist.slotLabel)
                color: Theme.textFaint
                font.pixelSize: Theme.fontMeta
            }
        }

        // No saves yet
        ColumnLayout {
            objectName: "saveEmptyBlock"
            visible: root.noSaves && !root.conflict && !root.unsupported
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 4
            FbLabel { text: qsTr("No saves yet"); font.pixelSize: Theme.fontBody; font.weight: Font.DemiBold }
            FbLabel {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("The first save appears in slot “%1” after you play.").arg(root.hist.slotLabel)
                color: Theme.textMeta
                font.pixelSize: Theme.fontMeta
            }
        }

        // Loading
        FbLabel {
            objectName: "saveLoading"
            visible: !root.unsupported && !root.noSaves && !root.conflict && root.hist.current.revision === undefined
            Layout.fillWidth: true
            text: root.hist.loading ? qsTr("Loading…") : (root.game.saveText || "")
            color: Theme.textMeta
            font.pixelSize: Theme.fontMeta
        }

        // Current version row: dot | slot + version + meta | pill
        RowLayout {
            objectName: "saveCurrentRow"
            visible: !root.unsupported && !root.conflict && root.hist.current.revision !== undefined
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 10
            Item {
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                Layout.alignment: Qt.AlignTop
                Rectangle {
                    anchors.centerIn: parent
                    width: 16; height: 16; radius: 8
                    color: "transparent"; border.width: 1; border.color: Theme.accent
                }
                Rectangle { anchors.centerIn: parent; width: 10; height: 10; radius: 5; color: Theme.accent }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 1
                spacing: 2
                RowLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 8
                    FbLabel {
                        objectName: "saveSlotName"
                        text: root.hist.slotLabel
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        Layout.maximumWidth: 160
                    }
                    FbMono {
                        objectName: "saveVersion"
                        text: root.hist.current.revisionText || ""
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSmall
                    }
                    Item { Layout.fillWidth: true }
                }
                FbLabel {
                    objectName: "saveMeta"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    wrapMode: Text.WordWrap
                    color: Theme.textMeta
                    font.pixelSize: Theme.fontMeta
                    text: root.offline ? qsTr("last synced %1").arg(root.hist.lastSynced)
                          : root.syncKind === "pending" ? qsTr("local changes not uploaded yet · this device")
                          : (root.hist.current.device ? qsTr("%1 · %2").arg(root.hist.current.when).arg(root.hist.current.device)
                                                      : (root.hist.current.when || ""))
                }
            }
            FbPill {
                objectName: "saveSyncPill"
                Layout.alignment: Qt.AlignTop
                visible: text !== ""
                small: true
                tone: root.offline ? "neutral" : (root.syncKind === "pending" ? "warn" : "ok")
                text: root.offline ? qsTr("Offline")
                      : root.syncKind === "pending" ? qsTr("⟳ Sync pending")
                      : root.syncKind === "synced" ? qsTr("✓ Synced") : ""
            }
        }

        FbLabel {
            objectName: "saveOfflineNote"
            visible: root.offline && !root.unsupported
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("You can play. The save syncs when the hub is back.")
            color: Theme.textMeta
            font.pixelSize: Theme.fontMeta
        }

        // Footer: "2 slots · 8 versions in Main"
        FbLabel {
            objectName: "saveCounts"
            visible: !root.unsupported && root.hist.current.revision !== undefined
            Layout.fillWidth: true
            Layout.leftMargin: 26
            elide: Text.ElideRight
            text: (root.hist.slotCount === 1 ? qsTr("1 slot") : qsTr("%1 slots").arg(root.hist.slotCount)) + " · "
                  + (root.hist.versionCount === 1 ? qsTr("1 version in %1") : qsTr("%1 versions in %2").arg(root.hist.versionCount)).arg(root.hist.slotLabel)
            color: Theme.textFaint
            font.pixelSize: Theme.fontMeta
        }
    }
}
