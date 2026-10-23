import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Confirmation dialog of a button with `confirm = {title, text, confirm_label?, danger?}`. Initial focus is Cancel when
// danger, otherwise the confirm button; Esc cancels. Looks like a design-system Dialog (400 px on `surface`, `rule` frame,
// scrim, no shadow); a Popup so it can sit above any plugin slot.
Popup {
    id: root
    objectName: "pluginConfirmDialog"
    property bool dark: true
    property var confirm: ({})
    signal accepted()

    parent: Window.window ? Window.window.contentItem : null
    anchors.centerIn: parent
    modal: true
    dim: true
    focus: true
    width: Math.min(Theme.dialog, parent ? parent.width - 2 * Theme.space2 : Theme.dialog)
    padding: 28
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    readonly property bool danger: confirm.danger === true

    background: Rectangle {
        color: Theme.surface
        border.width: 1
        border.color: Theme.rule
    }
    onOpened: (root.danger ? cancelBtn : okBtn).forceActiveFocus()

    contentItem: ColumnLayout {
        spacing: Theme.space15
        Text {
            textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
            objectName: "pluginConfirmTitle"
            Layout.fillWidth: true
            text: root.confirm.title || ""
            wrapMode: Text.WordWrap
            font.family: Theme.sans
            font.pixelSize: Theme.roles.dialogTitle.size
            font.letterSpacing: Theme.roles.dialogTitle.spacing
            color: Theme.ink
        }
        Text {
            textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
            Layout.fillWidth: true
            visible: text !== ""
            text: root.confirm.text || ""
            wrapMode: Text.WordWrap
            font.family: Theme.sans
            font.pixelSize: Theme.roles.body.size
            color: Theme.ink2
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.space1
            spacing: Theme.space1
            Item { Layout.fillWidth: true }
            UzButton {
                id: cancelBtn
                objectName: "pluginConfirmCancel"
                text: qsTr("Cancel")
                onClicked: root.close()
            }
            UzButton {
                id: okBtn
                objectName: "pluginConfirmOk"
                kind: root.danger ? "danger" : "primary"
                text: root.confirm.confirm_label || qsTr("Confirm")
                textFormat: Text.PlainText
                onClicked: { root.close(); root.accepted() }
            }
        }
    }
}
