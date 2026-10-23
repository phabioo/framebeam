import QtQuick
import QtQuick.Layouts
import Uzume.App

// Dialogs and the Toast of the Users page (3n-app-2, 3n-app-3, 3m-app-2), one instance over the page:
//   ask(spec)        { title, text, action, danger, run(finish) }  asks before an action; danger puts the action at the far right.
//                    run starts the call and reports through finish(ok, errorText); the dialog stays open (busy) until then.
//                    Optional secondary: { action, run() } adds a button next to Cancel (the Remove dialog: "Download saves...");
//                    the dialog stays open, and the caller reports progress with askNote("busy" | "done" | "error", text).
//   openInvite()     "Create an invite": Valid for, trust the first PC.
//   openMakeAdmin(u) the API needs a password of at least 12 characters for a new admin.
//   showCode(spec)   { label, title, text, code, note }  a secret shown once in the large invite-code style, with Copy.
//   toast(text, action, handler)  a Toast bottom left; `action` ("Undo") calls handler.
// Nothing here logs or stores a code; the code is cleared when its dialog closes.
Item {
    id: root
    required property var page
    property var spec: ({})
    property var codeSpec: ({})
    property string codeText: ""
    property var adminUser: null
    property string toastText: ""
    property string toastAction: ""
    property var toastHandler: null

    function ask(s: var) {
        askDialog.error = ""
        askDialog.busy = false
        askDialog.secondaryBusy = false
        askDialog.note = ""
        root.spec = s
        askDialog.open()
    }
    // Progress of the secondary action of the open ask dialog: state "busy" (button busy), "done" or "error" (a line under the text).
    function askNote(state: string, text: string) {
        askDialog.secondaryBusy = state === "busy"
        askDialog.note = state === "busy" ? "" : text
        askDialog.noteKind = state === "error" ? "error" : "done"
    }
    // Asks for one name (rename a person or a device, add a person): spec { title, text, label, value, action, max, run(value, finish) }.
    function askName(s: var) {
        root.nameSpec = s
        nameDialog.error = ""
        nameDialog.busy = false
        nameField.text = s.value ?? ""
        nameDialog.open()
        nameField.forceActiveFocus()
    }
    property var nameSpec: ({})
    function openInvite() {
        inviteDialog.error = ""
        inviteDialog.busy = false
        validFor.current = "7d"
        trustFirst.checked = false
        inviteDialog.open()
    }
    function openMakeAdmin(u: var) {
        root.adminUser = u
        adminDialog.error = ""
        adminDialog.busy = false
        adminPassword.text = ""
        adminDialog.open()
    }
    function showCode(s: var) {
        root.codeSpec = s
        root.codeText = s.code ?? ""
        codeDialog.open()
        copied.visible = false
    }
    function toast(text: string, action: string, handler: var) {
        root.toastText = text
        root.toastAction = action
        root.toastHandler = handler
        toastTimer.restart()
    }
    function hideToast() {
        toastTimer.stop()
        root.toastText = ""
        root.toastHandler = null
    }

    // The clipboard has no QML API in Qt 6.4: a hidden TextEdit copies.
    TextEdit { id: clip; visible: false }
    function copyCode() {
        clip.text = root.codeText
        clip.selectAll()
        clip.copy()
        clip.text = ""
        copied.visible = true
    }

    Timer { id: toastTimer; interval: 8000; onTriggered: root.hideToast() }
    UzToast {
        objectName: "usersToast"
        visible: root.toastText !== ""
        x: Theme.space3
        y: parent.height - height - Theme.space3
        z: 50
        text: root.toastText
        action: root.toastAction
        onActionTriggered: {
            const h = root.toastHandler
            root.hideToast()
            if (h) h()
        }
        onCloseRequested: root.hideToast()
    }

    // ---- ask
    UzDialog {
        id: askDialog
        objectName: "usersAskDialog"
        label: qsTr("Users")
        title: root.spec.title ?? ""
        text: root.spec.text ?? ""
        property bool secondaryBusy: false
        property string note: ""
        property string noteKind: "done"
        function run() {
            askDialog.error = ""
            askDialog.busy = true
            root.spec.run((ok, err) => {
                askDialog.busy = false
                if (ok) askDialog.close()
                else askDialog.error = err
            })
        }
        // The secondary action sits above Cancel / Remove: three buttons in the row do not fit the 400 px frame with wide fonts.
        UzButton {
            objectName: "askSecondary"
            kind: "ghost"
            visible: root.spec.secondary !== undefined
            text: root.spec.secondary ? root.spec.secondary.action : ""
            enabled: !askDialog.busy && !askDialog.secondaryBusy
            busy: askDialog.secondaryBusy
            onClicked: root.spec.secondary.run()
        }
        UzStatusIcon {
            objectName: "askNote"
            visible: askDialog.note !== ""
            kind: askDialog.noteKind
            small: true
            text: askDialog.note
            maxWidth: 344
        }
        actions: [
            UzButton { objectName: "askCancel"; text: qsTr("Cancel"); enabled: !askDialog.busy; onClicked: askDialog.close() },
            UzButton {
                objectName: "askConfirm"
                kind: "primary"
                visible: root.spec.danger !== true
                text: root.spec.action ?? ""
                busy: askDialog.busy
                onClicked: askDialog.run()
            }
        ]
        dangerActions: [
            UzButton {
                objectName: "askDanger"
                kind: "danger"
                visible: root.spec.danger === true
                text: root.spec.action ?? ""
                busy: askDialog.busy
                onClicked: askDialog.run()
            }
        ]
    }

    // ---- one name
    UzDialog {
        id: nameDialog
        objectName: "usersNameDialog"
        label: qsTr("Users")
        title: root.nameSpec.title ?? ""
        text: root.nameSpec.text ?? ""
        function submit() {
            const v = nameField.text.trim()
            if (v === "" || nameDialog.busy) return
            nameDialog.error = ""
            nameDialog.busy = true
            root.nameSpec.run(v, (ok, err) => {
                nameDialog.busy = false
                if (ok) nameDialog.close()
                else nameDialog.error = err
            })
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space05
            UzMono { text: (root.nameSpec.label ?? "").toUpperCase() }
            UzField {
                id: nameField
                objectName: "usersNameField"
                Layout.fillWidth: true
                fill: Theme.ground
                maximumLength: root.nameSpec.max ?? 32
                Accessible.name: root.nameSpec.label ?? ""
                onAccepted: nameDialog.submit()
            }
        }
        actions: [
            UzButton { objectName: "nameCancel"; text: qsTr("Cancel"); enabled: !nameDialog.busy; onClicked: nameDialog.close() },
            UzButton {
                objectName: "nameConfirm"
                kind: "primary"
                text: root.nameSpec.action ?? ""
                busy: nameDialog.busy
                enabled: nameField.text.trim() !== ""
                onClicked: nameDialog.submit()
            }
        ]
    }

    // ---- create an invite
    UzDialog {
        id: inviteDialog
        objectName: "usersInviteDialog"
        label: qsTr("Users · Invites")
        title: qsTr("Create an invite")
        text: qsTr("A friend enters the code in the Uzume app to join this server.")
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space1
            UzMono { text: qsTr("Valid for").toUpperCase() }
            UzSelect {
                id: validFor
                objectName: "inviteValidFor"
                Layout.fillWidth: true
                current: "7d"
                model: [
                    { value: "15m", label: qsTr("15 minutes") },
                    { value: "1h", label: qsTr("1 hour") },
                    { value: "24h", label: qsTr("24 hours") },
                    { value: "7d", label: qsTr("7 days") }
                ]
                onPicked: (value) => current = value
            }
            UzCheckbox {
                id: trustFirst
                objectName: "inviteTrustFirst"
                Layout.topMargin: Theme.space1
                text: qsTr("Trust the first PC that uses the code")
                hint: qsTr("It joins without waiting for your approval.")
            }
        }
        actions: [
            UzButton { objectName: "inviteCancel"; text: qsTr("Cancel"); enabled: !inviteDialog.busy; onClicked: inviteDialog.close() },
            UzButton {
                objectName: "inviteCreate"
                kind: "primary"
                text: qsTr("Create invite")
                busy: inviteDialog.busy
                onClicked: {
                    inviteDialog.error = ""
                    inviteDialog.busy = true
                    root.page.createInvite(validFor.current, trustFirst.checked, (ok, err) => {
                        inviteDialog.busy = false
                        if (ok) inviteDialog.close()
                        else inviteDialog.error = err
                    })
                }
            }
        ]
    }

    // ---- make admin
    UzDialog {
        id: adminDialog
        objectName: "usersAdminDialog"
        readonly property string who: root.adminUser !== null ? root.adminUser.display_name : ""
        label: qsTr("Users")
        title: qsTr("Make %1 an admin?").arg(adminDialog.who)
        text: qsTr("%1 can change everything on this server, including users and backups. Choose a password of at least 12 characters for %1; it opens the admin tools.").arg(adminDialog.who)
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space05
            UzMono { text: qsTr("Password for %1").arg(adminDialog.who).toUpperCase() }
            UzField {
                id: adminPassword
                objectName: "adminPassword"
                Layout.fillWidth: true
                echoMode: TextInput.Password
                fill: Theme.ground
                invalid: text.length > 0 && text.length < 12
                Accessible.name: qsTr("Password")
                onAccepted: adminDialog.submit()
            }
            UzStatusIcon {
                visible: adminPassword.invalid
                kind: "warning"
                small: true
                text: qsTr("At least 12 characters.")
            }
        }
        function submit() {
            if (adminPassword.text.length < 12 || adminDialog.busy) return
            adminDialog.error = ""
            adminDialog.busy = true
            root.page.setRole(root.adminUser, "admin", adminPassword.text, (ok, err) => {
                adminDialog.busy = false
                if (ok) { adminPassword.text = ""; adminDialog.close() }
                else adminDialog.error = err
            })
        }
        onClosed: adminPassword.text = ""
        actions: [
            UzButton { objectName: "adminCancel"; text: qsTr("Cancel"); enabled: !adminDialog.busy; onClicked: adminDialog.close() },
            UzButton {
                objectName: "adminConfirm"
                kind: "primary"
                text: qsTr("Make admin")
                busy: adminDialog.busy
                enabled: adminPassword.text.length >= 12
                onClicked: adminDialog.submit()
            }
        ]
    }

    // ---- a secret shown once
    UzDialog {
        id: codeDialog
        objectName: "usersCodeDialog"
        label: root.codeSpec.label ?? ""
        title: root.codeSpec.title ?? ""
        text: root.codeSpec.text ?? ""
        closable: false
        onClosed: { root.codeText = ""; root.codeSpec = ({}) }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space1
            Text {
                objectName: "codeValue"
                Layout.fillWidth: true
                Layout.topMargin: Theme.space1
                wrapMode: Text.WrapAnywhere
                text: root.codeText
                textFormat: Text.PlainText
                font.family: Theme.mono
                font.pixelSize: root.codeText.length > 14 ? 20 : Theme.roles.inviteCode.size
                font.weight: Theme.roles.inviteCode.weight
                font.letterSpacing: Theme.roles.inviteCode.spacing
                color: Theme.ink
            }
            UzMono { Layout.fillWidth: true; wrapMode: Text.Wrap; text: (root.codeSpec.note ?? "").toUpperCase() }
            UzStatusIcon { id: copied; objectName: "codeCopied"; visible: false; kind: "done"; small: true; text: qsTr("Copied") }
        }
        actions: [
            UzButton { objectName: "codeCopy"; text: qsTr("Copy"); onClicked: root.copyCode() },
            UzButton { objectName: "codeDone"; kind: "primary"; text: qsTr("Done"); onClicked: codeDialog.close() }
        ]
    }
}
