import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Uzume.App

// "Confirm it's you" (accounts-and-server.md 8f-1 and 8f-3): the password dialog that dangerous server steps ask for. One
// global instance sits in Main.qml, bound to `player.admin`; it opens by itself while `controller.confirmNeeded` is true and
// closes when the confirmation went through. Independent of the admin pages: `label`, `title` and `text` can be replaced
// (the label names the next step) and any other component with the same surface can be its `controller`:
//   confirmNeeded (bool), confirmState ("asking" | "checking" | "wrong" | "locked" | "unreachable" | "unset"),
//   confirmError (string), confirm(password), cancelConfirm().
// States: wrong password, locked (with the countdown inside confirmError), checking (the dialog is busy), server unreachable.
// Cancel and Esc call cancelConfirm(); Enter in the field or the primary button call confirm().
UzDialog {
    id: root
    objectName: "confirmDialog"
    property var controller: null
    readonly property bool needed: root.controller !== null && root.controller.confirmNeeded && !root.controller.confirmAutomatic
    readonly property string askState: root.controller !== null ? root.controller.confirmState : ""
    readonly property bool canConfirm: password.text !== "" && root.askState !== "checking" && root.askState !== "locked"

    label: qsTr("Confirm it's you")
    title: qsTr("Confirm it's you")
    text: qsTr("Enter your password. You won't be asked again for 5 minutes on this PC.")
    closable: false
    busy: root.askState === "checking"
    error: root.controller !== null && root.askState !== "checking" ? root.controller.confirmError : ""

    function submit() {
        if (root.canConfirm) root.controller.confirm(password.text)
    }
    function reset() { password.text = "" }

    onNeededChanged: {
        if (root.needed) {
            root.reset()
            root.open()
        } else {
            root.shown = false   // not close(): a confirmation that just went through must not wait for the busy state
            root.reset()
        }
    }
    Component.onCompleted: if (root.needed) root.open()
    onClosed: if (root.controller !== null) root.controller.cancelConfirm()   // Esc
    onShownChanged: if (root.shown) password.forceActiveFocus()
    // A wrong password keeps the dialog open: select it so typing replaces it.
    onAskStateChanged: if (root.shown && (root.askState === "wrong" || root.askState === "asking")) {
        password.selectAll()
        password.forceActiveFocus()
    }

    UzField {
        id: password
        objectName: "confirmPassword"
        Layout.fillWidth: true
        fill: Theme.ground
        echoMode: TextInput.Password
        placeholderText: qsTr("Password")
        invalid: root.askState === "wrong"
        enabled: root.askState !== "checking"
        Accessible.name: qsTr("Password")
        onAccepted: root.submit()
    }

    actions: [
        UzButton {
            objectName: "confirmCancel"
            text: qsTr("Cancel")
            enabled: root.askState !== "checking"
            onClicked: root.controller.cancelConfirm()
        },
        UzButton {
            objectName: "confirmSubmit"
            kind: "primary"
            text: root.askState === "checking" ? qsTr("Checking…") : qsTr("Confirm")
            enabled: root.canConfirm
            onClicked: root.submit()
        }
    ]
}
