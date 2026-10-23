import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// textfield block: mono input (UzField, design-system.md "TextField"); Enter emits action(action, text). `error` draws the
// danger frame and the message below.
ColumnLayout {
    id: root
    property var node: ({})
    property bool dark: true
    property string pluginId: ""
    signal action(string action, var value)

    spacing: Theme.space05
    UzField {
        id: field
        objectName: "pluginTextFieldInput"
        Layout.fillWidth: true
        text: root.node.text || ""
        placeholderText: root.node.placeholder || ""
        mono: true
        invalid: !!root.node.error
        onAccepted: root.action(root.node.action || "", text)
    }
    Text {
        textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
        objectName: "pluginTextFieldError"
        visible: !!root.node.error
        Layout.fillWidth: true
        text: root.node.error || ""
        wrapMode: Text.WordWrap
        font.pixelSize: Theme.roles.small.size
        color: Theme.danger
    }
}
