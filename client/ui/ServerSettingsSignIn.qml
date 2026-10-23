import QtQuick
import QtQuick.Layouts
import Uzume.App

// Server › Sign-in (accounts-and-server.md 3q-app-3) from GET / PUT /admin/accounts/settings: Passwords (Optional for members /
// Required for everyone). A changed row carries the changed marker and the page shows what the change does and Save / Cancel; the
// server asks Confirm it's you for the save.
// Left out, the API carries nothing for it: Show names on sign-in, Wrong tries before a wait, Stay signed in, and the names of people
// who would have to set a password (the policy does not touch users without one, they keep signing in by approval).
// objectNames: settingsSignIn, signinPasswordsRow, signinPasswords, signinWarning, signinSave, signinCancel.
ColumnLayout {
    id: root
    objectName: "settingsSignIn"
    required property var pane
    readonly property bool saved: root.pane.accounts.require_passwords === true
    property var draft: null                  // null = unchanged, else the chosen require_passwords
    readonly property bool wanted: root.draft !== null ? root.draft : root.saved
    readonly property bool changed: root.draft !== null && root.draft !== root.saved
    spacing: Theme.space3

    Component.onCompleted: root.pane.helpItems = [
        { title: qsTr("Admins"), text: qsTr("Admins always need a password of at least 12 characters.") },
        { title: qsTr("Who is affected"), text: qsTr("People without a password are not touched. They keep signing in by approval until they set one.") }
    ]

    function save() {
        root.pane.setBusy("signin", true)
        root.pane.call("PUT", "/admin/accounts/settings", { require_passwords: root.wanted }, function (ok, data, code, message) {
            root.pane.setBusy("signin", false)
            if (ok && data && data.require_passwords !== undefined) {
                root.pane.accounts = data
                root.draft = null
                root.pane.say(data.require_passwords ? qsTr("Everyone needs a password now.") : qsTr("Members can sign in without a password again."), "done")
            } else {
                root.pane.say(root.pane.failText(qsTr("Could not save"), message), "error")
            }
        })
    }

    UzSettingsRow {
        objectName: "signinPasswordsRow"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        resetMode: "none"
        idKey: "passwords"
        label: qsTr("Passwords")
        description: qsTr("Optional: members can remove their password. Required: nobody can. Admins always need 12 or more characters.")
        ctrl: "select"
        controlName: "signinPasswords"
        values: [{ value: "optional", label: qsTr("Optional for members") }, { value: "required", label: qsTr("Required for everyone") }]
        current: root.wanted ? "required" : "optional"
        changed: root.changed
        onValuePicked: value => root.draft = value === "required"
    }
    ColumnLayout {
        objectName: "signinWarning"
        visible: root.changed
        Layout.fillWidth: true
        spacing: Theme.space2
        UzStatusIcon {
            kind: "warning"
            small: true
            maxWidth: 640
            text: root.wanted ? qsTr("Members can no longer remove their password. People who have none are not signed out and keep signing in by approval until they set one.")
                                : qsTr("Members can remove their password again. Nobody is signed out.")
        }
        RowLayout {
            spacing: Theme.space1
            UzButton {
                objectName: "signinSave"
                kind: "primary"
                busy: root.pane.isBusy("signin")
                text: qsTr("Save")
                onClicked: root.save()
            }
            UzButton { objectName: "signinCancel"; text: qsTr("Cancel"); onClicked: root.draft = null }
        }
    }
}
