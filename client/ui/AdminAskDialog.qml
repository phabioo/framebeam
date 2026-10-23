import QtQuick
import QtQuick.Layouts
import Uzume.App

// The question dialog of the admin pieces that live inside other screens. It covers the whole window (not the small item
// it is declared in) and is the one place that copies text to the clipboard.
//   ask(spec)  spec = { label, title, text, action, danger, input, inputLabel, inputText, maxLength, run(value, finish) }
//              inputOptional: true allows an empty value; danger puts the action at the far right (danger button); input: true shows a text field (rename); run starts the
//              call and reports through finish(ok, errorText); the dialog stays open (busy) until then and shows the error.
//   copy(text) puts the text on the clipboard (the clipboard has no QML API in Qt 6.4: a hidden TextEdit copies).
// objectNames: adminAskDialog, adminAskInput, adminAskCancel, adminAskConfirm, adminAskDanger.
UzDialog {
    id: root
    objectName: "adminAskDialog"
    property var spec: ({})
    readonly property string value: field.text
    readonly property bool valid: root.spec.input !== true || root.spec.inputOptional === true || field.text.trim() !== ""
    parent: Window.contentItem

    function ask(s: var) {
        root.error = ""
        root.busy = false
        root.spec = s
        field.text = s.inputText ?? ""
        root.open()
        if (s.input === true) field.forceActiveFocus()
    }
    function copy(text: string) {
        clip.text = text
        clip.selectAll()
        clip.copy()
        clip.text = ""
    }
    function submit() {
        if (!root.valid || root.busy) return
        root.error = ""
        root.busy = true
        root.spec.run(field.text.trim(), (ok, err) => {
            root.busy = false
            if (ok) root.close()
            else root.error = err
        })
    }

    label: root.spec.label ?? ""
    title: root.spec.title ?? ""
    text: root.spec.text ?? ""
    onClosed: field.text = ""

    TextEdit { id: clip; visible: false }
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.spec.input === true
        spacing: Theme.space05
        UzMono { visible: (root.spec.inputLabel ?? "") !== ""; text: (root.spec.inputLabel ?? "").toUpperCase() }
        UzField {
            id: field
            objectName: "adminAskInput"
            Layout.fillWidth: true
            fill: Theme.ground
            maximumLength: root.spec.maxLength ?? 100
            Accessible.name: root.spec.inputLabel ?? ""
            onAccepted: root.submit()
        }
    }
    actions: [
        UzButton { objectName: "adminAskCancel"; text: qsTr("Cancel"); enabled: !root.busy; onClicked: root.close() },
        UzButton {
            objectName: "adminAskConfirm"
            kind: "primary"
            visible: root.spec.danger !== true
            text: root.spec.action ?? ""
            enabled: root.valid
            busy: root.busy
            onClicked: root.submit()
        }
    ]
    dangerActions: [
        UzButton {
            objectName: "adminAskDanger"
            kind: "danger"
            visible: root.spec.danger === true
            text: root.spec.action ?? ""
            busy: root.busy
            onClicked: root.submit()
        }
    ]
}
