import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// 3a: Connect to a hub.
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
                    Logo { size: 22 }
                    FbLabel { text: qsTr("FrameBeam Player"); font.pixelSize: 16; font.weight: Font.DemiBold }
                }
                FbLabel {
                    text: qsTr("Connect to a hub")
                    font.pixelSize: Theme.fontHero
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
                        onTrustCertificateRequested: fingerprint => root.player.trustChangedCertificate(fingerprint)
                        onCancelCertificateRequested: root.player.cancelCertificateChange()
                    }
                }
                FbLabel {
                    visible: root.player.hubs.length === 0
                    text: qsTr("No hub saved yet. Enter the address of your FrameBeam Hub below.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSmall
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
                        placeholderText: qsTr("Hub address, e.g. hub.local:8443")
                        inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText
                        onAccepted: addButton.clicked()
                    }
                    FbButton {
                        id: addButton
                        objectName: "addButton"
                        implicitHeight: 44
                        busyOnClick: true
                        text: qsTr("Add hub")
                        onClicked: root.player.addHub(addressField.text)
                    }
                }
                FbLabel {
                    visible: root.player.connectionNotice !== ""
                    Layout.fillWidth: true
                    text: root.player.connectionNotice
                    color: Theme.error
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
            }

            // Hub on this PC (0.9): install it or connect to it, create the admin, done.
            Rectangle {
                id: localCard
                objectName: "localHubCard"
                readonly property var local: root.player.localHub
                readonly property bool installing: local.phase === "installing"
                visible: local.available
                Layout.fillWidth: true
                implicitHeight: localCol.implicitHeight + 28
                radius: Theme.radius10
                color: Theme.surface
                border.width: 1
                border.color: Theme.borderCard
                ColumnLayout {
                    id: localCol
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: Theme.space8
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space12
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            FbLabel { text: qsTr("Set up a Hub on this PC"); font.pixelSize: Theme.fontSection; font.weight: Font.DemiBold }
                            FbLabel {
                                Layout.fillWidth: true
                                text: localCard.local.present ? qsTr("Connect to the Hub on this PC and sign in as its admin.")
                                                              : qsTr("Install the FrameBeam Hub on this PC to keep your games and saves here.")
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSmall
                                wrapMode: Text.WordWrap
                            }
                        }
                        FbButton {
                            objectName: "setupLocalHubButton"
                            kind: "primary"
                            busy: localCard.installing || localCard.local.phase === "connecting"
                            enabled: !busy
                            text: localCard.local.present ? qsTr("Connect") : qsTr("Set up")
                            onClicked: localCard.local.start()
                        }
                    }
                    FbLabel {
                        objectName: "localHubInstallText"
                        visible: localCard.installing && localCard.local.installText !== ""
                        text: localCard.local.installText
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                    }
                    ProgressBar {
                        visible: localCard.installing && value > 0 && value < 1
                        Layout.fillWidth: true
                        value: localCard.local.installProgress
                    }
                    FbLabel {
                        objectName: "localHubError"
                        visible: localCard.local.phase === "error" && localCard.local.error !== ""
                        Layout.fillWidth: true
                        text: localCard.local.error
                        color: Theme.error
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                    }
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
                    text: qsTr("Connect automatically to the last used hub on startup")
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
