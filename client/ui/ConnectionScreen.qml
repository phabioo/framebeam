import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FrameBeam.Player

// 3a: Mit einem Hub verbinden.
Rectangle {
    id: root
    required property PlayerController player
    color: Theme.bg

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight + 64
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { }

        ColumnLayout {
            id: column
            width: Math.min(620, flick.width - 32)
            x: (flick.width - width) / 2
            y: Math.max(32, (flick.height - implicitHeight) / 2)
            spacing: 28

            ColumnLayout {
                spacing: 18
                RowLayout {
                    spacing: 10
                    Logo { }
                    FbLabel { text: qsTr("FrameBeam Player"); font.pixelSize: 16; font.weight: Font.DemiBold }
                }
                FbLabel {
                    text: qsTr("Mit einem Hub verbinden")
                    font.pixelSize: 30
                    font.weight: Font.DemiBold
                    font.letterSpacing: -0.6
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10
                Repeater {
                    model: root.player.hubs
                    delegate: HubCard {
                        required property var modelData
                        Layout.fillWidth: true
                        hub: modelData
                        onConnectRequested: hubId => root.player.connectProfile(hubId)
                        onRetryRequested: root.player.retryConnection()
                        onRemoveRequested: hubId => root.player.removeHub(hubId)
                    }
                }
                FbLabel {
                    visible: root.player.hubs.length === 0
                    text: qsTr("Noch kein Hub gespeichert. Gib unten die Adresse deines FrameBeam Hubs ein.")
                    color: Theme.textMuted
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    FbField {
                        id: addressField
                        objectName: "addressField"
                        Layout.fillWidth: true
                        placeholderText: qsTr("Hub-Adresse, z. B. hub.local:8443")
                        inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText
                        onAccepted: addButton.clicked()
                    }
                    FbButton {
                        id: addButton
                        objectName: "addButton"
                        implicitHeight: 44
                        text: qsTr("Hub hinzufügen")
                        onClicked: root.player.addHub(addressField.text)
                    }
                }
                FbLabel {
                    visible: root.player.connectionNotice !== ""
                    Layout.fillWidth: true
                    text: root.player.connectionNotice
                    color: Theme.error
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 14
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borderSidebar }
                FbToggle {
                    id: autoToggle
                    objectName: "autoConnectToggle"
                    Layout.fillWidth: true
                    text: qsTr("Beim Start automatisch mit dem zuletzt verwendeten Hub verbinden")
                    checked: root.player.autoConnect
                    onToggled: root.player.autoConnect = checked
                }
                FbMono {
                    Layout.fillWidth: true
                    text: root.player.deviceFooter
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
