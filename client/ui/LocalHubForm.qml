import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// "Set up a Hub on this PC": step of the pairing screen. needsSetup = "Create the Hub admin" (user name, password
// twice), needsSignIn = "Sign in as a Hub admin". The Hub on this PC pairs the Player directly, no approval needed.
ColumnLayout {
    id: root
    required property PlayerController player
    readonly property var local: player.localHub
    readonly property bool setup: local.phase === "needsSetup"
    readonly property bool working: local.phase === "working"
    spacing: 6

    FbLabel {
        Layout.fillWidth: true
        text: root.setup ? qsTr("Create the Hub admin") : qsTr("Sign in as a Hub admin")
        font.pixelSize: Theme.fontBody
        font.weight: Font.Medium
    }
    FbLabel {
        Layout.fillWidth: true
        text: root.setup ? qsTr("This Hub has no admin yet. The admin manages games, users and settings on the Hub.")
                         : qsTr("Use the user name and password of an admin of the Hub on this PC. This Player is approved right away.")
        color: Theme.textMuted
        font.pixelSize: Theme.fontMeta
        wrapMode: Text.WordWrap
    }
    Eyebrow { text: qsTr("User name"); Layout.topMargin: 4 }
    FbField {
        id: userField
        objectName: "localUserField"
        Layout.fillWidth: true
        font.family: Qt.application.font.family
        enabled: !root.working
        inputMethodHints: Qt.ImhNoPredictiveText
        onAccepted: passField.forceActiveFocus()
    }
    Eyebrow { text: qsTr("Password"); Layout.topMargin: 4 }
    FbField {
        id: passField
        objectName: "localPasswordField"
        Layout.fillWidth: true
        echoMode: TextInput.Password
        font.family: Qt.application.font.family
        enabled: !root.working
        onAccepted: root.setup ? againField.forceActiveFocus() : submitButton.clicked()
    }
    Eyebrow { text: qsTr("Password again"); Layout.topMargin: 4; visible: root.setup }
    FbField {
        id: againField
        objectName: "localPasswordAgainField"
        visible: root.setup
        Layout.fillWidth: true
        echoMode: TextInput.Password
        font.family: Qt.application.font.family
        enabled: !root.working
        onAccepted: submitButton.clicked()
    }
    FbLabel {
        objectName: "localHubError"
        visible: root.local.error !== ""
        Layout.fillWidth: true
        text: root.local.error
        color: Theme.error
        font.pixelSize: Theme.fontMeta
        wrapMode: Text.WordWrap
    }
    FbButton {
        id: submitButton
        objectName: "localSubmitButton"
        Layout.topMargin: 4
        kind: "primary"
        busy: root.working
        enabled: !root.working
        text: root.working ? qsTr("Connecting…") : (root.setup ? qsTr("Create admin and connect") : qsTr("Sign in and connect"))
        onClicked: root.local.submit(userField.text, passField.text, againField.text)
    }
}
