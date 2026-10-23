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
        Layout.leftMargin: Theme.space15
        Layout.rightMargin: Theme.space15
        Layout.bottomMargin: Theme.space1
        spacing: Theme.space1
        Repeater {
            model: root.columns
            delegate: Text {
                textFormat: Text.PlainText
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.minimumWidth: 0
                text: String(modelData).toUpperCase()
                elide: Text.ElideRight
                font.family: Theme.mono
                font.pixelSize: Theme.roles.tableHeader.size
                font.weight: Theme.roles.tableHeader.weight
                font.letterSpacing: Theme.roles.tableHeader.spacing
                color: Theme.blockColor("textFaint", root.dark)
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
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            clip: true
            implicitHeight: Theme.row36
            onActivated: root.action(modelData.action, modelData.value)
            Rectangle {
                anchors.fill: parent
                color: row.hovered ? Theme.blockColor("hover", root.dark) : "transparent"
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.tok("rule2", root.dark)
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.space15
                anchors.rightMargin: Theme.space15
                spacing: Theme.space1
                Repeater {
                    model: row.modelData.cells || []
                    delegate: Text {
                        textFormat: Text.PlainText
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.minimumWidth: 0
                        text: String(modelData)
                        elide: Text.ElideRight
                        font.pixelSize: Theme.roles.input.size
                        color: index === 0 ? Theme.blockColor("text", root.dark) : Theme.blockColor("textSecondary", root.dark)
                    }
                }
            }
        }
    }
}
