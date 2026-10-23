import QtQuick
import QtQuick.Layouts
import Uzume.App

// 8d-6 Add someone to this PC: sign in with a name and password, or use an invite; without a password an admin approves this
// PC (waiting, declined). The person joins those on this PC; the others stay signed in.
StartPage {
    id: root
    required property PlayerController player
    readonly property var ac: player.accounts
    signal forgotRequested()
    screenLabel: qsTr("Switch user · add someone")
    openLabel: qsTr("UZUME · SWITCH USER")
    initialFocus: ac.addWaiting || ac.addDenied ? null : (ac.addView === "invite" ? invite.codeField.field : signIn.nameField.field)
    hints: [{ kbd: "Tab", label: qsTr("Next") }, { kbd: "Enter", label: qsTr("Sign in") }, { pad: "A", label: qsTr("Choose") }, { pad: "Y", label: qsTr("Keyboard") },
            { kbd: "Esc", pad: "B", label: qsTr("Back") }]
    onBackRequested: ac.cancelAdd()

    StartHead {
        number: 1
        label: root.ac.serverName
        title: qsTr("Add someone to this PC")
        text: root.ac.addView === "invite" ? qsTr("Use the invite code you were given and pick the name others will see.")
                                           : qsTr("Sign in with a name and password. The others stay signed in.")
    }
    StartSignInForm {
        id: signIn
        visible: root.ac.addView === "sign_in" && !root.ac.addWaiting && !root.ac.addDenied
        prefix: "addSignIn"
        inviteText: qsTr("Use an invite")
        approvalLine: qsTr("No password? Sign in with your name only and an admin approves this PC.")
        busy: root.ac.busy
        errorCode: root.ac.errorCode
        errorMessage: root.ac.errorMessage
        retryAfter: root.ac.retryAfter
        onSignedIn: (name, password) => root.ac.addSignIn(name, password)
        onInviteRequested: root.ac.addShowInvite()
        onForgotRequested: root.forgotRequested()
        onApprovalRequested: root.ac.addRequestApproval()
    }
    StartInviteForm {
        id: invite
        visible: root.ac.addView === "invite" && !root.ac.addWaiting && !root.ac.addDenied
        prefix: "addInvite"
        serverName: root.ac.serverName
        busy: root.ac.busy
        errorCode: root.ac.errorCode
        errorMessage: root.ac.errorMessage
        retryAfter: root.ac.retryAfter
        joinText: qsTr("Join")
        onJoined: (code, name, password) => root.ac.addRedeemInvite(code, name, password)
        onHaveAccount: root.ac.addShowSignIn()
    }
    StartApprovalBlock {
        visible: root.ac.addWaiting || root.ac.addDenied
        prefix: "addApproval"
        mode: root.ac.addWaiting ? "wait" : "denied"
        deviceName: root.player.deviceName
        platformText: root.player.platformText.split(" ")[0]
        serverName: root.ac.serverName
        onAsk: root.ac.addRequestApproval()
        onCancel: root.ac.addCancelApproval()
        onBack: root.ac.addShowSignIn()
    }
    RowLayout {
        Layout.fillWidth: true
        UzButton { objectName: "addCancel"; kind: "ghost"; size: "sm"; text: qsTr("Cancel"); onClicked: root.ac.cancelAdd() }
        Item { Layout.fillWidth: true }
    }
    Text {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        text: qsTr("After signing in, the person joins those on this PC and can add a PIN in My account.")
        textFormat: Text.PlainText
        font.family: Theme.sans
        font.pixelSize: Theme.roles.small.size
        lineHeight: Theme.roles.small.line
        lineHeightMode: Text.FixedHeight
        color: Theme.muted
    }
}
