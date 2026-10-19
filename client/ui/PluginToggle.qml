import QtQuick
import QtQuick.Layouts

// toggle block: optional label left, switch right. Emits action(action, newBool).
PluginFocusable {
    id: root
    property var node: ({})
    property string pluginId: ""
    readonly property bool checked: node.checked === true
    signal action(string action, var value)

    ringRadius: 11
    implicitHeight: 28
    implicitWidth: (label.visible ? label.implicitWidth + 12 : 0) + 40
    onActivated: root.action(node.action || "", !checked)

    Text {
        textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
        id: label
        visible: text !== ""
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(0, parent.width - 52)
        elide: Text.ElideRight
        text: root.node.label || ""
        font.pixelSize: Theme.fontSmall
        color: Theme.blockColor("textSecondary", root.dark)
    }
    Rectangle {
        objectName: "pluginToggleSwitch"
        width: 40
        height: 22
        radius: 11
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        color: root.checked ? Theme.blockColor("accent", root.dark) : Theme.blockColor("border", root.dark)
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
        Rectangle {
            x: root.checked ? 21 : 3
            y: 3
            width: 16
            height: 16
            radius: 8
            color: root.checked ? Theme.blockColor("textOnAccent", root.dark) : Theme.blockColor("text", root.dark)
            Behavior on x { NumberAnimation { duration: Theme.durFast; easing.type: Easing.OutCubic } }
        }
    }
}
