import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Confirmation dialog of a button with `confirm = {title, text, confirm_label?, danger?}`. Initial focus is Cancel when
// danger, otherwise the confirm button; Esc cancels.
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
    width: Math.min(420, parent ? parent.width - 32 : 420)
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    readonly property bool danger: confirm.danger === true

    background: Rectangle {
        radius: Theme.radius12
        color: Theme.blockColor("popup", root.dark)
        border.width: 1
        border.color: root.danger ? Theme.toneLine("danger", root.dark) : Theme.blockColor("popupBorder", root.dark)
    }
    onOpened: (root.danger ? cancelBtn : okBtn).forceActiveFocus()

    component DlgButton: PluginFocusable {
        id: b
        property string label: ""
        property bool primary: false
        property bool danger: false
        ringRadius: Theme.radius7
        implicitHeight: 34
        implicitWidth: Math.max(72, text.implicitWidth + 28)
        Layout.minimumWidth: implicitWidth
        Rectangle {
            anchors.fill: parent
            radius: Theme.radius7
            color: b.primary ? (b.hovered ? Qt.lighter(Theme.blockColor("accent", b.dark), 1.1) : Theme.blockColor("accent", b.dark))
                   : b.danger ? (b.hovered ? Theme.toneLine("danger", b.dark) : Theme.toneBgFor("danger", b.dark))
                   : (b.hovered ? Theme.blockColor("hover", b.dark) : "transparent")
            border.width: b.primary ? 0 : 1
            border.color: b.danger ? Theme.toneLine("danger", b.dark) : Theme.blockColor("borderButton", b.dark)
            Text {
                id: text
                anchors.centerIn: parent
                text: b.label
                font.pixelSize: Theme.fontBody
                font.weight: b.primary ? Font.DemiBold : Font.Medium
                color: b.primary ? Theme.blockColor("textOnAccent", b.dark) : (b.danger ? Theme.toneFg("danger", b.dark) : Theme.blockColor("text", b.dark))
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 12
        Text {
            objectName: "pluginConfirmTitle"
            Layout.fillWidth: true
            text: root.confirm.title || ""
            wrapMode: Text.WordWrap
            font.pixelSize: 17
            font.weight: Font.DemiBold
            color: Theme.blockColor("text", root.dark)
        }
        Text {
            Layout.fillWidth: true
            visible: text !== ""
            text: root.confirm.text || ""
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontSmall
            color: Theme.blockColor("textSecondary", root.dark)
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 8
            spacing: 8
            Item { Layout.fillWidth: true }
            DlgButton {
                id: cancelBtn
                objectName: "pluginConfirmCancel"
                dark: root.dark
                label: qsTr("Cancel")
                onActivated: root.close()
            }
            DlgButton {
                id: okBtn
                objectName: "pluginConfirmOk"
                dark: root.dark
                label: root.confirm.confirm_label || qsTr("Confirm")
                primary: !root.danger
                danger: root.danger
                onActivated: { root.close(); root.accepted() }
            }
        }
    }
}
