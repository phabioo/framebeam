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
            width: Math.min(520, flick.width - 32)
            x: (flick.width - width) / 2
            y: Math.max(32, (flick.height - implicitHeight) / 2)
            spacing: 28

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 14
                RowLayout {
                    Layout.bottomMargin: 10
                    spacing: 10
                    Logo { size: 22 }
                    FbLabel { text: qsTr("FrameBeam Player"); font.pixelSize: 16; font.weight: Font.DemiBold }
                }
                Eyebrow {
                    objectName: "connectEyebrow"
                    text: root.player.hubs.length === 0 ? qsTr("First start") : qsTr("Connect")
                }
                FbLabel {
                    objectName: "connectTitle"
                    text: qsTr("Connect to a Hub")
                    font.pixelSize: Theme.fontHero
                    font.weight: Font.DemiBold
                    font.letterSpacing: -0.6
                }
                FbLabel {
                    objectName: "connectBody"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontBody
                    lineHeight: 1.4
                    text: qsTr("Enter the address of your FrameBeam Hub. You find it in the Hub under Settings → General.")
                }
            }

            // A positioner with explicit widths, not a layout: a ColumnLayout filled by a Repeater while this screen was not shown
            // was never re-run and left the saved-Hub card at 0 px (R-P2, the card lost its name and background).
            Column {
                id: hubList
                objectName: "hubList"
                Layout.fillWidth: true
                spacing: 10
                Repeater {
                    model: root.player.hubs
                    delegate: HubCard {
                        required property var modelData
                        width: hubList.width
                        hub: modelData
                        onConnectRequested: hubId => root.player.connectProfile(hubId)
                        onRetryRequested: root.player.retryConnection()
                        onRemoveRequested: hubId => {
                            // Discarding an attempt also empties the address field it came from (R-P20).
                            if (!modelData.saved) addressField.text = ""
                            root.player.removeHub(hubId)
                        }
                        onTrustCertificateRequested: fingerprint => root.player.trustChangedCertificate(fingerprint)
                        onCancelCertificateRequested: root.player.cancelCertificateChange()
                    }
                }
                FbLabel {
                    visible: root.player.hubs.length === 0
                    text: qsTr("No Hub saved yet.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                    width: hubList.width
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                FbLabel { text: qsTr("Hub address"); font.pixelSize: Theme.fontSmall; font.weight: Font.Medium }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    FbField {
                        id: addressField
                        objectName: "addressField"
                        Layout.fillWidth: true
                        placeholderText: qsTr("hub.example.com:8443")
                        inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText
                        onAccepted: addButton.clicked()
                    }
                    FbButton {
                        id: addButton
                        objectName: "addButton"
                        implicitHeight: 44
                        busyOnClick: true
                        kind: "primary"
                        text: qsTr("Connect")
                        onClicked: root.player.addHub(addressField.text)
                    }
                }
                FbLabel {
                    objectName: "connectHint"
                    visible: root.player.connectionNotice === ""
                    text: qsTr("host:port, without https://")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontMeta
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
                    text: qsTr("Connect automatically on startup")
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
