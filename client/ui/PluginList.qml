import QtQuick
import QtQuick.Layouts

// list block: rows { id?, title, sub?, meta?, image?, tone?, action?, value? }; a row is a focus stop when it has an action.
// `selected` is a row id. Emits action(row.action, row.value).
ColumnLayout {
    id: root
    property var node: ({})
    property bool dark: true
    property string pluginId: ""
    signal action(string action, var value)

    Layout.minimumWidth: 0
    spacing: 2
    Repeater {
        model: root.node.rows || []
        delegate: PluginFocusable {
            id: row
            required property var modelData
            required property int index
            objectName: "pluginListRow_" + index
            readonly property bool clickable: !!modelData.action
            readonly property bool isSelected: modelData.id !== undefined && String(modelData.id) === String(root.node.selected)
            dark: root.dark
            actionable: clickable
            ringRadius: Theme.radius6
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            clip: true
            implicitHeight: Math.max(36, content.implicitHeight + 12)
            onActivated: root.action(modelData.action, modelData.value)

            Rectangle {
                anchors.fill: parent
                radius: Theme.radius6
                color: row.isSelected ? Theme.blockColor("selection", root.dark) : (row.hovered ? Theme.blockColor("hover", root.dark) : "transparent")
                border.width: row.isSelected ? 1 : 0
                border.color: Theme.toneLine("info", root.dark)
            }
            RowLayout {
                id: content
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 10
                PluginImage {
                    visible: !!row.modelData.image
                    dark: root.dark
                    pluginId: root.pluginId
                    src: row.modelData.image || ""
                    size: "m"
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 1
                    Text {
                        textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                        Layout.fillWidth: true
                        text: row.modelData.title || ""
                        elide: Text.ElideRight
                        font.pixelSize: Theme.fontSmall
                        font.weight: Font.Medium
                        color: Theme.blockColor("text", root.dark)
                    }
                    Text {
                        textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                        visible: !!row.modelData.sub
                        Layout.fillWidth: true
                        text: row.modelData.sub || ""
                        elide: Text.ElideRight
                        font.pixelSize: Theme.fontMeta
                        color: Theme.blockColor("textMeta", root.dark)
                    }
                }
                Text {
                    textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                    visible: !!row.modelData.meta
                    Layout.maximumWidth: Math.max(40, content.width * 0.4)
                    elide: Text.ElideRight
                    text: row.modelData.meta || ""
                    font.family: Theme.mono
                    font.pixelSize: Theme.fontMeta
                    color: row.modelData.tone ? Theme.toneFg(row.modelData.tone, root.dark) : Theme.blockColor("textMuted", root.dark)
                }
                Text {
                    textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                    visible: row.clickable
                    text: "›"
                    font.pixelSize: Theme.fontCard
                    color: Theme.blockColor("textFaint", root.dark)
                }
            }
        }
    }
}
