import QtQuick
import QtQuick.Layouts
import Uzume.App

// 8d-7c / 8d-7e: the person's session ran out or the PIN is locked, so the password is needed once. The name is fixed.
StartPage {
    id: root
    required property PlayerController player
    readonly property var ac: player.accounts
    readonly property var person: ac.people.find(p => p.userId === ac.targetUserId) || ({})
    readonly property bool pinLocked: person.pinLocked === true
    signal forgotRequested()
    screenLabel: qsTr("Switch user · password")
    openLabel: qsTr("UZUME · SWITCH USER · %1").arg(ac.targetName)
    initialFocus: form.passwordField.field
    hints: [{ kbd: "Enter", label: qsTr("Sign in") }, { pad: "A", label: qsTr("Choose") }, { pad: "Y", label: qsTr("Keyboard") },
            { kbd: "Esc", pad: "B", label: qsTr("Back") }]
    onBackRequested: ac.keepPlaying()

    StartHead {
        number: 1
        label: root.ac.serverName
        title: root.pinLocked ? qsTr("PIN locked") : qsTr("Sign in again")
        text: root.pinLocked ? qsTr("Enter your password once to unlock the PIN on this PC.")
                             : qsTr("Your session on this PC ran out after 90 days. Enter your password once.")
    }
    StartSignInForm {
        id: form
        prefix: "passwordAgain"
        nameText: root.ac.targetName
        nameReadOnly: true
        showInvite: false
        showApproval: false
        busy: root.ac.busy
        errorCode: root.ac.errorCode
        errorMessage: root.ac.errorMessage
        retryAfter: root.ac.retryAfter
        onSignedIn: (name, password) => root.ac.signInWithPassword(name, password)
        onForgotRequested: root.forgotRequested()
    }
    UzButton {
        objectName: "passwordAgainBack"
        kind: "ghost"
        size: "sm"
        text: qsTr("Keep playing as before")
        Layout.alignment: Qt.AlignLeft
        onClicked: root.ac.keepPlaying()
    }
}
