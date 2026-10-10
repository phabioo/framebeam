import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// 3b: Add hub (hub found, confirm certificate, approve device).
Rectangle {
    id: root
    required property PlayerController player
    readonly property var info: player.pairing
    readonly property string phase: info.phase
    property bool inviteMode: false
    // Approval step is ready for input (request approval or redeem an invite)
    // "Set up a Hub on this PC": the Hub on this PC pairs the Player with an admin sign-in instead of an approval.
    readonly property var local: player.localHub
    readonly property bool localForm: local.phase === "needsSetup" || local.phase === "needsSignIn" || local.phase === "working"
    readonly property bool canChoose: !root.localForm && (root.phase === "needsPairing" || root.phase === "denied" || root.phase === "expired")
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
            width: Math.min(640, flick.width - 32)
            x: (flick.width - width) / 2
            y: Math.max(32, (flick.height - implicitHeight) / 2)
            spacing: 14

            ColumnLayout {
                spacing: 6
                Layout.bottomMargin: 8
                FbLabel {
                    text: root.localForm ? qsTr("Hub on this PC") : qsTr("Add hub")
                    font.pixelSize: Theme.fontHero
                    font.weight: Font.DemiBold
                    font.letterSpacing: -0.6
                }
                FbMono { text: root.info.address; color: Theme.textMuted; font.pixelSize: 13 }
            }

            // Step 1: hub found
            StepCard {
                Layout.fillWidth: true
                objectName: "stepHub"
                title: root.info.hubKnown ? qsTr("Hub detected") : qsTr("Searching for hub…")
                stage: root.info.hubKnown ? "done" : "active"
                FbMono {
                    visible: root.info.hubKnown
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    text: qsTr("%1 · FrameBeam Hub %2 · Protocol v%3 · compatible")
                              .arg(root.info.hubName).arg(root.info.hubVersion).arg(root.info.protocol)
                }
            }

            // Step 2: certificate (TOFU, explicit confirmation)
            StepCard {
                id: certStep
                Layout.fillWidth: true
                objectName: "stepCertificate"
                title: root.phase === "trust" ? qsTr("Check certificate")
                       : (certStep.stage === "done" ? (root.info.tls ? qsTr("Certificate trusted") : qsTr("Unencrypted connection")) : qsTr("Certificate"))
                stage: !root.info.hubKnown ? "pending" : (root.phase === "trust" ? "active" : "done")

                FbMono {
                    visible: root.info.tls && root.info.hubKnown
                    objectName: "fingerprintText"
                    Layout.fillWidth: true
                    text: root.info.fingerprint
                    color: Theme.text
                    font.pixelSize: Theme.fontMeta
                    lineHeight: 1.35
                }
                FbLabel {
                    visible: root.phase === "trust"
                    Layout.fillWidth: true
                    text: qsTr("SHA-256 fingerprint of the hub certificate. Compare it with the one shown in the hub and trust it only if both match. If the certificate changes later, the connection is blocked.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontMeta
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    visible: root.phase === "trust"
                    Layout.topMargin: 4
                    spacing: 10
                    FbButton {
                        objectName: "trustButton"
                        kind: "primary"
                        busyOnClick: true
                        text: qsTr("Trust fingerprint")
                        onClicked: root.player.confirmTrust()
                    }
                    FbButton {
                        objectName: "rejectButton"
                        text: qsTr("Cancel")
                        onClicked: root.player.rejectTrust()
                    }
                }
                FbLabel {
                    visible: root.phase !== "trust" && root.info.hubKnown
                    Layout.fillWidth: true
                    text: root.info.tls ? qsTr("Saved. If the certificate changes later, the connection is blocked.")
                                        : qsTr("Development only: this hub is contacted without TLS, so there is no fingerprint.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontMeta
                    wrapMode: Text.WordWrap
                }
            }

            // Step 3: approve device
            StepCard {
                id: approvalStep
                Layout.fillWidth: true
                objectName: "stepApproval"
                title: root.localForm ? qsTr("Sign in") : qsTr("Approve device")
                stage: (root.phase === "trust" || !root.info.hubKnown) ? "pending" : "active"

                FbSegment {
                    objectName: "pairingModeSegment"
                    visible: root.canChoose
                    options: [
                        { value: "request", label: qsTr("Request approval"), name: "requestSegment" },
                        { value: "invite", label: qsTr("I have an invite code"), name: "inviteSegment" }
                    ]
                    current: root.inviteMode ? "invite" : "request"
                    onPicked: value => root.inviteMode = (value === "invite")
                }

                LocalHubForm {
                    objectName: "localHubForm"
                    visible: root.localForm
                    Layout.fillWidth: true
                    player: root.player
                }

                ColumnLayout {
                    objectName: "inviteForm"
                    visible: root.canChoose && root.inviteMode
                    Layout.fillWidth: true
                    spacing: 6
                    Eyebrow { text: qsTr("Invite code") }
                    FbField {
                        id: inviteCodeField
                        objectName: "inviteCodeField"
                        Layout.fillWidth: true
                        placeholderText: qsTr("FB-XXXX-XXXX")
                        font.capitalization: Font.AllUppercase
                        inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhUppercaseOnly
                        enabled: !root.info.inviteBusy
                        onAccepted: inviteNameField.forceActiveFocus()
                    }
                    Eyebrow { text: qsTr("Display name"); Layout.topMargin: 4 }
                    FbField {
                        id: inviteNameField
                        objectName: "inviteNameField"
                        Layout.fillWidth: true
                        maximumLength: 32
                        font.family: Qt.application.font.family
                        placeholderText: qsTr("How others see you on this Hub")
                        enabled: !root.info.inviteBusy
                        onAccepted: redeemButton.clicked()
                    }
                    FbLabel {
                        Layout.fillWidth: true
                        text: qsTr("The invite code comes from the Hub admin and works once. Your user is created on this Hub with the name above.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontMeta
                        wrapMode: Text.WordWrap
                    }
                }

                RowLayout {
                    visible: root.phase === "awaiting" || root.phase === "authenticating"
                    spacing: 12
                    BusyIndicator {
                        running: parent.visible
                        implicitWidth: 22
                        implicitHeight: 22
                        palette.dark: Theme.accent
                    }
                    ColumnLayout {
                        spacing: 2
                        FbLabel {
                            text: root.phase === "authenticating" ? qsTr("Signing in…") : qsTr("Waiting for admin approval")
                            font.pixelSize: Theme.fontBody
                            font.weight: Font.Medium
                        }
                        FbLabel {
                            visible: root.phase === "awaiting"
                            text: qsTr("The request appears in the hub under Clients.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontMeta
                        }
                    }
                }

                FbLabel {
                    visible: root.phase === "needsPairing" && !root.inviteMode && !root.localForm
                    Layout.fillWidth: true
                    text: qsTr("This device is not yet approved on the hub. The hub admin must confirm the request.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontMeta
                    wrapMode: Text.WordWrap
                }
                Rectangle {
                    visible: root.phase === "denied" || root.phase === "expired"
                    objectName: "pairingProblem"
                    Layout.fillWidth: true
                    implicitHeight: problemText.implicitHeight + 24
                    radius: 8
                    color: root.phase === "denied" ? Theme.errorBg : Theme.infoBg
                    FbLabel {
                        id: problemText
                        anchors.fill: parent
                        anchors.margins: 12
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.fontSmall
                        color: root.phase === "denied" ? Theme.errorText : Theme.infoText
                        text: root.phase === "denied" ? qsTr("The request was denied by the admin.")
                                                      : qsTr("The request has expired. Please request again.")
                    }
                }
                FbLabel {
                    visible: root.info.error !== "" && approvalStep.stage === "active"
                    Layout.fillWidth: true
                    text: root.info.error
                    color: Theme.error
                    font.pixelSize: Theme.fontMeta
                    wrapMode: Text.WordWrap
                }

                GridLayout {
                    visible: approvalStep.stage === "active" && !root.localForm
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 6
                    Layout.topMargin: 4
                    Eyebrow { text: qsTr("Device name"); Layout.preferredWidth: 130 }
                    FbMono { text: root.info.deviceName; color: Theme.text; Layout.fillWidth: true; elide: Text.ElideRight }
                    Eyebrow { text: qsTr("Platform"); Layout.preferredWidth: 130 }
                    FbMono { text: root.info.platform; color: Theme.text; Layout.fillWidth: true }
                    Eyebrow { text: qsTr("Player version"); Layout.preferredWidth: 130 }
                    FbMono { text: root.info.playerVersion; color: Theme.text; Layout.fillWidth: true }
                }

                RowLayout {
                    visible: approvalStep.stage === "active"
                    Layout.topMargin: 4
                    spacing: 10
                    FbButton {
                        objectName: "requestButton"
                        visible: root.canChoose && !root.inviteMode
                        kind: "primary"
                        busyOnClick: true
                        text: root.phase === "needsPairing" ? qsTr("Request approval") : qsTr("Request again")
                        onClicked: root.player.requestPairing()
                    }
                    FbButton {
                        id: redeemButton
                        objectName: "redeemButton"
                        visible: root.canChoose && root.inviteMode
                        kind: "primary"
                        enabled: !root.info.inviteBusy
                        busy: root.info.inviteBusy
                        text: root.info.inviteBusy ? qsTr("Redeeming…") : qsTr("Redeem invite")
                        onClicked: root.player.redeemInvite(inviteCodeField.text, inviteNameField.text)
                    }
                    FbButton {
                        objectName: "cancelRequestButton"
                        visible: root.phase === "awaiting"
                        text: qsTr("Cancel request")
                        onClicked: root.player.cancelPairing()
                    }
                    FbButton {
                        objectName: "backButton"
                        visible: root.phase !== "awaiting"
                        text: qsTr("Back")
                        onClicked: root.player.leavePairing()
                    }
                }
            }
        }
    }
}
