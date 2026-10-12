import QtQuick
import QtQuick.Layouts

// table block: columns = labels, rows { cells, action?, value? }. Equal column widths; a row is a focus stop with an action.
ColumnLayout {
    id: root
    property var node: ({})
    property bool dark: true
    property string pluginId: ""
    readonly property var columns: node.columns || []
    signal action(string action, var value)

    spacing: 0
    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 10
        Layout.rightMargin: 10
        Layout.bottomMargin: 6
        spacing: 8
        Repeater {
            model: root.columns
            delegate: Text {
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                text: String(modelData).toUpperCase()
                elide: Text.ElideRight
                font.family: Theme.mono
                font.pixelSize: Theme.fontMono
                font.letterSpacing: 0.6
                color: Theme.blockColor("textMeta", root.dark)
            }
        }
    }
    Repeater {
        model: root.node.rows || []
        delegate: PluginFocusable {
            id: row
            required property var modelData
            required property int index
            objectName: "pluginTableRow_" + index
            dark: root.dark
            actionable: !!modelData.action
            ringRadius: Theme.radius6
            Layout.fillWidth: true
            implicitHeight: 34
            onActivated: root.action(modelData.action, modelData.value)
            Rectangle {
                anchors.fill: parent
                radius: Theme.radius6
                color: row.hovered ? Theme.blockColor("hover", root.dark) : "transparent"
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.blockColor("cardBorder", root.dark)
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8
                Repeater {
                    model: row.modelData.cells || []
                    delegate: Text {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        text: String(modelData)
                        elide: Text.ElideRight
                        font.pixelSize: Theme.fontSmall
                        color: index === 0 ? Theme.blockColor("text", root.dark) : Theme.blockColor("textSecondary", root.dark)
                    }
                }
            }
        }
    }
}
