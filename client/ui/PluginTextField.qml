import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// textfield block: mono input; Enter emits action(action, text). `error` draws the danger line and the message below.
ColumnLayout {
    id: root
    property var node: ({})
    property bool dark: true
    property string pluginId: ""
    signal action(string action, var value)

    spacing: 4
    TextField {
        id: field
        objectName: "pluginTextFieldInput"
        Layout.fillWidth: true
        implicitHeight: 36
        text: root.node.text || ""
        placeholderText: root.node.placeholder || ""
        leftPadding: 12
        rightPadding: 12
        font.family: Theme.mono
        font.pixelSize: Theme.fontSmall
        color: Theme.blockColor("text", root.dark)
        placeholderTextColor: Theme.blockColor("textFaint", root.dark)
        selectionColor: Theme.blockColor("accent", root.dark)
        selectedTextColor: Theme.blockColor("textOnAccent", root.dark)
        verticalAlignment: TextInput.AlignVCenter
        onAccepted: root.action(root.node.action || "", text)
        background: Rectangle {
            radius: Theme.radius7
            color: Theme.blockColor("card", root.dark)
            border.width: root.node.error || field.activeFocus ? 2 : 1
            border.color: root.node.error ? Theme.toneFg("danger", root.dark) : (field.activeFocus ? Theme.blockColor("accent", root.dark) : Theme.blockColor("border", root.dark))
        }
    }
    Text {
        textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
        objectName: "pluginTextFieldError"
        visible: !!root.node.error
        Layout.fillWidth: true
        text: root.node.error || ""
        wrapMode: Text.WordWrap
        font.pixelSize: Theme.fontMeta
        color: Theme.toneFg("danger", root.dark)
    }
}
