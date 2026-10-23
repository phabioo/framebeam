import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Uzume.App

// The invite form of 8b-3 and 8d-6: Invite code, Your name (shown to everyone on the server), Password (optional but
// recommended), Join and "I already have an account". The caller owns the request (`joined(code, name, password)`).
ColumnLayout {
    id: root
    property string prefix: "invite"
    property string serverName: ""
    property string codeText: ""
    property bool busy: false
    property string errorCode: ""
    property string errorMessage: ""
    property int retryAfter: 0
    property string joinText: qsTr("Join")
    readonly property alias codeField: codeInput
    readonly property alias nameField: nameInput
    readonly property alias passwordField: passwordInput
    signal joined(string code, string name, string password)
    signal haveAccount()
    spacing: Theme.space2
    Layout.fillWidth: true

    function submit() { root.joined(codeInput.text.trim(), nameInput.text.trim(), passwordInput.text) }
    onCodeTextChanged: if (codeInput.text === "") codeInput.text = codeText
    Component.onCompleted: if (codeText !== "") codeInput.text = codeText

    StartField {
        id: codeInput
        name: root.prefix + "Code"
        label: qsTr("Invite code")
        placeholder: qsTr("UZ-XXXX-XXXX")
        mono: true
        error: root.errorCode === "invite_invalid" || root.errorCode === "invalid_invite_code" ? root.errorMessage : ""
        onAccepted: nameInput.field.forceActiveFocus()
    }
    StartField {
        id: nameInput
        name: root.prefix + "Name"
        label: qsTr("Your name")
        error: root.errorCode === "display_name_taken" ? root.errorMessage : ""
        hint: root.serverName !== "" ? qsTr("Shown to everyone on %1.").arg(root.serverName) : qsTr("Shown to everyone on the server.")
        onAccepted: passwordInput.field.forceActiveFocus()
    }
    StartField {
        id: passwordInput
        name: root.prefix + "Password"
        label: qsTr("Password (optional)")
        secret: true
        error: root.errorCode === "bad_request" ? root.errorMessage : ""
        hint: qsTr("Recommended. Lets you sign in on another PC without waiting for an admin. At least 12 characters.")
        onAccepted: root.submit()
    }
    StartNotice {
        code: ["invite_invalid", "invalid_invite_code", "display_name_taken", "bad_request"].indexOf(root.errorCode) >= 0 ? "" : root.errorCode
        message: root.errorMessage
        retryAfter: root.retryAfter
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space1
        UzButton {
            objectName: root.prefix + "Submit"
            kind: "primary"
            busy: root.busy
            enabled: !root.busy
            text: root.busy ? qsTr("Joining…") : root.joinText
            onClicked: root.submit()
        }
        UzButton {
            objectName: root.prefix + "HaveAccount"
            kind: "ghost"
            enabled: !root.busy
            text: qsTr("I already have an account")
            onClicked: root.haveAccount()
        }
        Item { Layout.fillWidth: true }
    }
}
