import QtQuick
import QtQuick.Layouts
import Uzume.App

// Server › Storage (accounts-and-server.md 3q-app-6). The API has no storage breakdown, so the page shows what GET /admin/status
// carries: the disk the data lives on as a bar (used / free, ink tints, never hues) with a hairline legend and the backup disk, a warning
// when the backup disk is nearly full, and the save history limits (network settings save_keep_recent / daily / weekly; apply at once).
// Left out, the API carries nothing for it: the split into Games, Saves and snapshots, Save states, Screenshots, Cores and firmware, and
// the Clean-up suggestions (old snapshots, upload leftovers, unused cores).
// objectNames: settingsStorage, storageBar, storageUsed, storageFree, storageBackupDisk, storageWarning, storageRecentRow, storageDailyRow,
// storageWeeklyRow, storageNoData.
ColumnLayout {
    id: root
    objectName: "settingsStorage"
    required property var pane
    readonly property var st: root.pane.admin.status
    readonly property var disk: root.st.disk ?? null
    readonly property var backupDisk: root.st.backup_disk ?? null
    readonly property real usedFraction: root.disk && root.disk.total_bytes > 0 ? (root.disk.total_bytes - root.disk.free_bytes) / root.disk.total_bytes : 0
    readonly property real backupFraction: root.backupDisk && root.backupDisk.total_bytes > 0 && root.backupDisk.free_bytes !== undefined
                                           ? (root.backupDisk.total_bytes - root.backupDisk.free_bytes) / root.backupDisk.total_bytes : -1
    spacing: Theme.space4

    Component.onCompleted: {
        root.pane.helpItems = [
            { title: qsTr("Save history"), text: qsTr("How many older versions of each save the server keeps. 0 keeps all of them. Manual snapshots and open conflicts are never removed.") },
            { title: qsTr("Clean-up"), text: qsTr("Clean-up never offers saves.") }
        ]
        root.pane.admin.refreshStatus()
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.maximumWidth: 640
        spacing: Theme.space2
        UzStatusIcon {
            objectName: "storageWarning"
            visible: root.backupFraction >= 0.9
            kind: "warning"
            maxWidth: 640
            text: root.backupFraction >= 0 ? qsTr("The backup disk is %1 % full. The next backup may not fit.").arg(Math.round(root.backupFraction * 100)) : ""
        }
        UzSectionLabel {
            Layout.fillWidth: true
            text: qsTr("Disk")
            meta: root.disk ? qsTr("%1 of %2").arg(root.pane.bytesText(root.disk.total_bytes - root.disk.free_bytes)).arg(root.pane.bytesText(root.disk.total_bytes)) : ""
        }
        Rectangle {
            objectName: "storageBar"
            visible: root.disk !== null
            Layout.fillWidth: true
            height: 8
            color: Theme.rule2
            Rectangle {
                height: parent.height
                width: parent.width * Math.min(1, root.usedFraction)
                color: Qt.alpha(Theme.ink, 0.72)
            }
        }
        ColumnLayout {
            visible: root.disk !== null
            Layout.fillWidth: true
            spacing: 0
            Repeater {
                model: root.disk ? [
                    { id: "used", label: qsTr("Used"), value: root.pane.bytesText(root.disk.total_bytes - root.disk.free_bytes) },
                    { id: "free", label: qsTr("Free"), value: root.pane.bytesText(root.disk.free_bytes) }
                ] : []
                delegate: Item {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 36
                    Text { x: 0; anchors.verticalCenter: parent.verticalCenter; text: modelData.label; textFormat: Text.PlainText; font.pixelSize: Theme.roles.input.size; color: Theme.ink }
                    Text {
                        objectName: modelData.id === "used" ? "storageUsed" : "storageFree"
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.value
                        textFormat: Text.PlainText
                        font.family: Theme.mono
                        font.pixelSize: Theme.roles.input.size
                        color: Theme.ink2
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule2 }
                }
            }
            Item {
                visible: root.backupDisk !== null && root.backupFraction >= 0
                Layout.fillWidth: true
                implicitHeight: 36
                Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Backup disk"); textFormat: Text.PlainText; font.pixelSize: Theme.roles.input.size; color: Theme.ink }
                Text {
                    objectName: "storageBackupDisk"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.backupDisk && root.backupFraction >= 0 ? qsTr("%1 % · %2 free").arg(Math.round(root.backupFraction * 100)).arg(root.pane.bytesText(root.backupDisk.free_bytes)) : ""
                    textFormat: Text.PlainText
                    font.family: Theme.mono
                    font.pixelSize: Theme.roles.input.size
                    color: Theme.ink2
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule2 }
            }
        }
        UzMono {
            objectName: "storageNoData"
            visible: root.disk === null
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("This server doesn't report its disk.")
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.space1
        UzSectionLabel { Layout.fillWidth: true; text: qsTr("Save history") }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            Repeater {
                model: [
                    { key: "save_keep_recent", id: "recent", row: "storageRecentRow", label: qsTr("Keep newest versions"), text: qsTr("Per save slot."), title: qsTr("Keep how many newest versions?") },
                    { key: "save_keep_daily", id: "daily", row: "storageDailyRow", label: qsTr("Keep daily versions"), text: qsTr("Days of which the newest version is kept."), title: qsTr("Keep how many daily versions?") },
                    { key: "save_keep_weekly", id: "weekly", row: "storageWeeklyRow", label: qsTr("Keep weekly versions"), text: qsTr("Weeks of which the newest version is kept."), title: qsTr("Keep how many weekly versions?") }
                ]
                delegate: UzSettingsRow {
                    required property var modelData
                    readonly property var s: root.pane.netSetting(modelData.key)
                    objectName: modelData.row
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    resetMode: s.source === "web" ? "reset" : "none"
                    changed: s.source === "web"
                    idKey: modelData.id
                    label: modelData.label
                    description: (s.value === "0" ? qsTr("All") : s.value) + " · " + modelData.text
                    ctrl: "buttons"
                    buttons: [{ name: "storageChange_" + modelData.id, text: qsTr("Change…") }]
                    onButtonClicked: root.pane.openEdit({
                        title: modelData.title, text: qsTr("0 keeps all of them. Manual snapshots and open conflicts are never removed."),
                        label: modelData.label, value: s.value, mono: true, action: qsTr("Change"),
                        save: function (v) {
                            if (!/^[0-9]+$/.test(v)) { root.pane.editFinished(false, qsTr("Enter a whole number.")); return }
                            root.pane.saveNet(modelData.key, { value: v }, qsTr("Saved %1.").arg(modelData.label.toLowerCase()))
                        }
                    })
                    onResetRequested: root.pane.resetNet(modelData.key, qsTr("%1 is back to the server's own value.").arg(modelData.label))
                }
            }
        }
    }
}
