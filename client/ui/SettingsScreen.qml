import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Player settings (Sidebar "Settings"): Appearance and Hubs (switch, remove, auto-connect).
Rectangle {
    id: root
    required property PlayerController player
    color: Theme.bg

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            Layout.fillHeight: true
            Layout.preferredWidth: 232
            player: root.player
        }

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: content.implicitHeight + 64
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: content
                x: 36
                y: 28
                width: Math.min(640, flick.width - 72)
                spacing: 22

                ColumnLayout {
                    spacing: 2
                    FbLabel { text: qsTr("Settings"); font.pixelSize: 26; font.weight: Font.DemiBold; font.letterSpacing: -0.4 }
                    FbLabel {
                        text: qsTr("Settings apply locally to this device")
                        color: Theme.textMuted
                        font.pixelSize: 13
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: appearanceCol.implicitHeight + 40
                    radius: 10
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.borderCard

                    ColumnLayout {
                        id: appearanceCol
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 12
                        Eyebrow { text: qsTr("Appearance") }
                        FbSegment {
                            objectName: "appearanceSegment"
                            options: [
                                { value: "dark", label: qsTr("Dark"), name: "appearanceDark" },
                                { value: "light", label: qsTr("Light"), name: "appearanceLight" },
                                { value: "system", label: qsTr("System"), name: "appearanceSystem" }
                            ]
                            current: root.player.appearance
                            onPicked: value => root.player.appearance = value
                        }
                        FbLabel {
                            Layout.fillWidth: true
                            color: Theme.textMuted
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            text: qsTr("System follows the color scheme of the operating system where it is available. The game view always stays dark.")
                        }
                    }
                }

                Rectangle {
                    objectName: "updatesSection"
                    Layout.fillWidth: true
                    implicitHeight: updatesCol.implicitHeight + 40
                    radius: 10
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.borderCard

                    ColumnLayout {
                        id: updatesCol
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 12
                        Eyebrow { text: qsTr("Updates") }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            FbLabel { text: qsTr("Version"); color: Theme.textMuted; font.pixelSize: 13 }
                            FbMono { objectName: "updatesVersion"; text: root.player.updates.currentVersion }
                            Item { Layout.fillWidth: true }
                            FbLabel {
                                objectName: "updatesChannelEffective"
                                text: qsTr("Channel: %1").arg(root.player.updates.effectiveChannel)
                                color: Theme.textMuted
                                font.pixelSize: 12
                            }
                        }
                        FbSegment {
                            objectName: "updateChannelSegment"
                            options: [
                                { value: "default", label: qsTr("Default (%1)").arg(root.player.updates.compiledChannel), name: "updateChannelDefault" },
                                { value: "stable", label: qsTr("Stable"), name: "updateChannelStable" },
                                { value: "test", label: qsTr("Test"), name: "updateChannelTest" }
                            ]
                            current: root.player.updates.channelSetting
                            onPicked: value => root.player.updates.setChannel(value)
                        }
                        FbToggle {
                            objectName: "updateAutoInstallToggle"
                            Layout.fillWidth: true
                            visible: root.player.updates.autoInstallAvailable
                            text: qsTr("Install updates automatically (applied at the next start, never during a game)")
                            checked: root.player.updates.autoInstall
                            onToggled: root.player.updates.setAutoInstall(checked)
                        }
                        FbLabel {
                            Layout.fillWidth: true
                            visible: !root.player.updates.autoInstallAvailable && root.player.updates.effectiveChannel === "stable"
                            color: Theme.textMuted
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            text: qsTr("Stable updates are only installed after you confirm.")
                        }
                        FbLabel {
                            objectName: "updatesStatus"
                            Layout.fillWidth: true
                            text: root.player.updates.statusText
                            wrapMode: Text.WordWrap
                            font.pixelSize: 13
                            color: root.player.updates.state === "error" ? Theme.toneColor("error") : Theme.text
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 6
                            radius: 3
                            visible: root.player.updates.state === "downloading"
                            color: Theme.borderRow
                            Rectangle {
                                width: parent.width * root.player.updates.progress
                                height: parent.height
                                radius: 3
                                color: Theme.accent
                            }
                        }
                        FbLabel {
                            text: qsTr("Last check: %1").arg(root.player.updates.lastCheckText)
                            color: Theme.textMuted
                            font.pixelSize: 12
                        }
                        RowLayout {
                            spacing: 10
                            FbButton {
                                objectName: "updatesCheckNow"
                                implicitHeight: 32
                                text: qsTr("Check now")
                                enabled: !root.player.updates.checking && root.player.updates.state !== "downloading"
                                onClicked: root.player.updates.checkNow()
                            }
                            FbButton {
                                objectName: "updatesInstall"
                                implicitHeight: 32
                                kind: "primary"
                                visible: root.player.updates.canInstall
                                text: root.player.updates.state === "ready" ? qsTr("Restart to update") : qsTr("Install and restart")
                                onClicked: root.player.updates.install()
                            }
                            FbButton {
                                objectName: "updatesNotes"
                                kind: "link"
                                visible: root.player.updates.notesUrl !== ""
                                text: qsTr("Release notes")
                                onClicked: root.player.updates.openNotes()
                            }
                        }
                    }
                }

                Rectangle {
                    objectName: "hubsSection"
                    Layout.fillWidth: true
                    implicitHeight: hubsCol.implicitHeight + 40
                    radius: 10
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.borderCard

                    ColumnLayout {
                        id: hubsCol
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 12
                        Eyebrow { text: qsTr("Hubs") }

                        Repeater {
                            model: root.player.hubs
                            delegate: Rectangle {
                                id: hubRow
                                required property var modelData
                                required property int index
                                property bool confirmRemove: false
                                visible: modelData.saved
                                Layout.fillWidth: true
                                Layout.preferredHeight: visible ? rowCol.implicitHeight + 24 : 0
                                radius: 8
                                color: Theme.bg
                                border.width: modelData.connected ? 1.5 : 1
                                border.color: modelData.connected ? Theme.accent : Theme.borderRow

                                ColumnLayout {
                                    id: rowCol
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 8
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 10
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            spacing: 0
                                            RowLayout {
                                                spacing: 8
                                                FbLabel {
                                                    objectName: "hubName_" + hubRow.modelData.hubId
                                                    Layout.fillWidth: true
                                                    text: hubRow.modelData.name
                                                    font.pixelSize: 14
                                                    font.weight: Font.DemiBold
                                                    elide: Text.ElideRight
                                                }
                                                FbLabel {
                                                    objectName: "hubCurrent_" + hubRow.modelData.hubId
                                                    visible: hubRow.modelData.connected
                                                    text: qsTr("Current Hub")
                                                    font.pixelSize: 11
                                                    color: Theme.accent
                                                }
                                            }
                                            FbMono { Layout.fillWidth: true; text: hubRow.modelData.detail; elide: Text.ElideRight }
                                        }
                                        StatusDot { tone: hubRow.modelData.connected ? "ok" : hubRow.modelData.tone }
                                        FbLabel {
                                            objectName: "hubState_" + hubRow.modelData.hubId
                                            text: hubRow.modelData.connected ? qsTr("Connected") : hubRow.modelData.statusText
                                            font.pixelSize: 12
                                            color: hubRow.modelData.tone === "neutral" ? Theme.textMuted : Theme.toneColor(hubRow.modelData.tone)
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 10
                                        visible: !hubRow.confirmRemove
                                        FbButton {
                                            objectName: "hubSwitch_" + hubRow.modelData.hubId
                                            visible: !hubRow.modelData.connected
                                            implicitHeight: 32
                                            kind: "primary"
                                            text: qsTr("Switch")
                                            enabled: hubRow.modelData.status !== "connecting"
                                            onClicked: root.player.switchToHub(hubRow.modelData.hubId)
                                        }
                                        Item { Layout.fillWidth: true }
                                        FbButton {
                                            objectName: "hubRemove_" + hubRow.modelData.hubId
                                            kind: "link"
                                            text: qsTr("Remove")
                                            onClicked: hubRow.confirmRemove = true
                                        }
                                    }
                                    RowLayout {
                                        objectName: "hubRemoveConfirm_" + hubRow.modelData.hubId
                                        Layout.fillWidth: true
                                        spacing: 10
                                        visible: hubRow.confirmRemove
                                        FbLabel {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            wrapMode: Text.WordWrap
                                            font.pixelSize: 12
                                            color: Theme.text
                                            text: hubRow.modelData.connected
                                                  ? qsTr("Remove this Hub from the Player? Running Sessions end and you return to the connection screen. Saves on the Hub are not deleted.")
                                                  : qsTr("Remove this Hub from the Player? Saves on the Hub are not deleted.")
                                        }
                                        FbButton {
                                            objectName: "hubRemoveCancel_" + hubRow.modelData.hubId
                                            implicitHeight: 32
                                            text: qsTr("Cancel")
                                            onClicked: hubRow.confirmRemove = false
                                        }
                                        FbButton {
                                            objectName: "hubRemoveConfirmButton_" + hubRow.modelData.hubId
                                            implicitHeight: 32
                                            kind: "primary"
                                            text: qsTr("Remove")
                                            onClicked: root.player.removeHub(hubRow.modelData.hubId)
                                        }
                                    }
                                }
                            }
                        }

                        FbLabel {
                            objectName: "hubsEmpty"
                            visible: root.player.hubs.length === 0
                            Layout.fillWidth: true
                            text: qsTr("No Hub saved yet.")
                            color: Theme.textMuted
                            font.pixelSize: 13
                        }

                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borderRow }
                        FbToggle {
                            objectName: "settingsAutoConnectToggle"
                            Layout.fillWidth: true
                            text: qsTr("Connect automatically to the last used Hub on startup")
                            checked: root.player.autoConnect
                            onToggled: root.player.autoConnect = checked
                        }
                        FbButton {
                            objectName: "settingsAddHubButton"
                            kind: "link"
                            text: qsTr("Add another Hub…")
                            onClicked: root.player.switchHub()
                        }
                    }
                }
            }
        }
    }
}
