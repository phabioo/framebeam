import QtQuick

// segmented block: 2 to 3 short options { value, label }; selected = value. Emits action(action, value).
Rectangle {
    id: root
    property var node: ({})
    property bool dark: true
    property string pluginId: ""
    signal action(string action, var value)

    implicitHeight: 36
    implicitWidth: row.implicitWidth + 6
    radius: Theme.radius7
    color: Theme.blockColor("card", dark)
    border.width: 1
    border.color: Theme.blockColor("cardBorder", dark)

    Row {
        id: row
        x: 3
        y: 3
        spacing: 2
        height: parent.height - 6
        Repeater {
            model: root.node.options || []
            delegate: PluginFocusable {
                id: seg
                required property var modelData
                objectName: "pluginSegment_" + modelData.value
                readonly property bool active: String(root.node.selected) === String(modelData.value)
                dark: root.dark
                ringRadius: Theme.radius5
                height: row.height
                implicitWidth: segLabel.implicitWidth + 24
                width: implicitWidth
                onActivated: root.action(root.node.action || "", modelData.value)
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radius5
                    color: seg.active ? Theme.blockColor("border", root.dark) : (seg.hovered ? Theme.blockColor("hover", root.dark) : "transparent")
                }
                Text {
                    textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                    id: segLabel
                    anchors.centerIn: parent
                    text: seg.modelData.label
                    font.pixelSize: Theme.fontSmall
                    font.weight: seg.active ? Font.DemiBold : Font.Medium
                    color: seg.active ? Theme.blockColor("text", root.dark) : Theme.blockColor("textMuted", root.dark)
                }
            }
        }
    }
}
