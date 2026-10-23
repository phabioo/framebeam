import QtQuick
import QtQuick.Layouts
import Uzume.App

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
    // 3c-5: the game of this summary is paused in the background; the checkpoint was written when it was paused.
    readonly property bool running: game.running === true
    readonly property var bg: player.backgroundGame
    property real nowMs: Date.now()
    function ago(ms) {
        const s = Math.max(0, Math.round((nowMs - ms) / 1000))
        return s < 60 ? qsTr("%1 s").arg(s) : (s < 3600 ? qsTr("%1 min").arg(Math.floor(s / 60)) : qsTr("%1 h").arg(Math.floor(s / 3600)))
    }
    Timer { running: root.running; interval: 10000; repeat: true; onTriggered: root.nowMs = Date.now() }
    onRunningChanged: nowMs = Date.now()
    signal manage()
    signal uploadRequested()

    objectName: "saveSummary"
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    Layout.preferredWidth: 1
    implicitHeight: col.implicitHeight + 2 * Theme.space1 + 1
    // An open block (no card): a `rule` hairline above, the content on the ground.
    color: "transparent"
    Rectangle { width: parent.width; height: 1; color: Theme.rule }

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.topMargin: Theme.space1 + 1
        anchors.bottomMargin: Theme.space1
        anchors.leftMargin: 0
        anchors.rightMargin: 0
        spacing: Theme.space1

        // Header: eyebrow + link
        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            implicitHeight: Theme.row28
            Eyebrow {
                anchors.left: parent.left
                anchors.right: link.left
                anchors.rightMargin: Theme.space1
                anchors.verticalCenter: parent.verticalCenter
                text: root.conflict ? qsTr("▲ Save conflict") : qsTr("Save")
                color: root.conflict ? Theme.accentText : Theme.dim
            }
            UzButton {
                id: link
                objectName: "manageSavesLink"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                // Never wider than the card (the label elides): the eyebrow keeps at least 60 px.
                width: Math.max(0, Math.min(implicitWidth, parent.width - 68))
                visible: !root.unsupported
                kind: "link"
                implicitHeight: Theme.row28
                font.pixelSize: Theme.roles.input.size
                font.weight: Font.Medium
                text: root.noSaves ? (root.hist.canUploadFile && root.hist.uploadBlockReason === "" ? qsTr("Upload save file…") : qsTr("Manage saves →"))
                      : root.offline ? qsTr("View saves →") : qsTr("Manage saves →")
                onClicked: root.noSaves && root.hist.canUploadFile && root.hist.uploadBlockReason === "" ? root.uploadRequested() : root.manage()
            }
        }

        // Hub without save sync (or user unknown): the old "Save" row text
        UzLabel {
            objectName: "saveUnsupported"
            visible: root.unsupported
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: root.game.saveText || ""
            color: Theme.muted
            font.pixelSize: Theme.roles.input.size
        }

        // Conflict
        ColumnLayout {
            objectName: "saveConflictBlock"
            visible: root.conflict && !root.unsupported
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: Theme.space1
            UzLabel {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("The server and this device have different saves for “%1”. Nothing is overwritten until you decide.").arg(root.hist.slotLabel)
                color: Theme.ink2
                font.pixelSize: Theme.roles.input.size
            }
            UzButton {
                objectName: "resolveConflictButton"
                implicitHeight: Theme.row32
                font.pixelSize: Theme.roles.input.size
                text: qsTr("Resolve conflict")
                enabled: root.game.canPlay === true && !root.hist.confirmationOpen
                onClicked: root.player.playSelected()
            }
            UzLabel {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("The conflict dialog opens when the game starts. Restore, upload and new slot are paused for “%1” until it is resolved.").arg(root.hist.slotLabel)
                color: Theme.dim
                font.pixelSize: Theme.roles.small.size
            }
        }

        // No saves yet
        ColumnLayout {
            objectName: "saveEmptyBlock"
            visible: root.noSaves && !root.conflict && !root.unsupported
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: Theme.space05
            UzLabel { text: qsTr("No saves yet"); font.pixelSize: Theme.roles.body.size; font.weight: Font.DemiBold }
            UzLabel {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("The first save appears in slot “%1” after you play.").arg(root.hist.slotLabel)
                color: Theme.muted
                font.pixelSize: Theme.roles.small.size
            }
        }

        // Loading
        UzLabel {
            objectName: "saveLoading"
            visible: !root.unsupported && !root.noSaves && !root.conflict && root.hist.current.revision === undefined
            Layout.fillWidth: true
            text: root.hist.loading ? qsTr("Loading…") : (root.game.saveText || "")
            color: Theme.muted
            font.pixelSize: Theme.roles.small.size
        }

        // Current version row: dot | slot + version + meta | pill
        RowLayout {
            objectName: "saveCurrentRow"
            visible: !root.unsupported && !root.conflict && root.hist.current.revision !== undefined
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: Theme.space15
            Item {
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                Layout.alignment: Qt.AlignTop
                Rectangle {
                    anchors.centerIn: parent
                    width: 16; height: 16;
                    color: "transparent"; border.width: 1; border.color: Theme.accent
                }
                Rectangle { anchors.centerIn: parent; width: 10; height: 10; color: Theme.accent }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 1
                spacing: 2
                RowLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: Theme.space1
                    UzLabel {
                        objectName: "saveSlotName"
                        text: root.hist.slotLabel
                        font.pixelSize: Theme.roles.body.size
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        Layout.maximumWidth: 160
                    }
                    UzMono {
                        objectName: "saveVersion"
                        text: root.hist.current.revisionText || ""
                        color: Theme.muted
                        font.pixelSize: Theme.roles.input.size
                    }
                    Item { Layout.fillWidth: true }
                }
                UzLabel {
                    objectName: "saveMeta"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    wrapMode: Text.WordWrap
                    color: Theme.muted
                    font.pixelSize: Theme.roles.small.size
                    text: root.running && root.bg.pausedMs ? qsTr("checkpoint %1 ago · this device").arg(root.ago(root.bg.pausedMs))
                          : root.offline ? qsTr("last synced %1").arg(root.hist.lastSynced)
                          : root.syncKind === "pending" ? qsTr("local changes not uploaded yet · this device")
                          : (root.hist.current.device ? qsTr("%1 · %2").arg(root.hist.current.when).arg(root.hist.current.device)
                                                      : (root.hist.current.when || ""))
                }
            }
            UzTag {
                objectName: "saveSyncPill"
                Layout.alignment: Qt.AlignTop
                visible: text !== ""
                small: true
                tone: root.running || root.offline ? "neutral" : (root.syncKind === "pending" ? "warn" : "ok")
                text: root.running ? qsTr("Syncs on quit")
                      : root.offline ? qsTr("Offline")
                      : root.syncKind === "pending" ? qsTr("⟳ Sync pending")
                      : root.syncKind === "synced" ? qsTr("✓ Synced") : ""
            }
        }

        UzLabel {
            objectName: "saveOfflineNote"
            visible: root.offline && !root.unsupported
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("You can play. The save syncs when the server is back.")
            color: Theme.muted
            font.pixelSize: Theme.roles.small.size
        }

        // Footer: "2 slots · 8 versions in Main"
        UzLabel {
            objectName: "saveCounts"
            visible: !root.unsupported && root.hist.current.revision !== undefined
            Layout.fillWidth: true
            Layout.leftMargin: 16 + Theme.space15
            elide: Text.ElideRight
            text: Theme.plural(root.hist.slotCount, qsTr("slot"), qsTr("slots")) + " · "
                  + (root.hist.versionCount === 1 ? qsTr("1 version in %1") : qsTr("%1 versions in %2").arg(root.hist.versionCount)).arg(root.hist.slotLabel)
            color: Theme.dim
            font.pixelSize: Theme.roles.small.size
        }
    }
}
