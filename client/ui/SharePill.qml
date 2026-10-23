import QtQuick
import QtQuick.Layouts
import Uzume.App

// Share state of the game header and panel: square dot + text. on = shared (ok), else dim. bare = no plane (meta-11).
Rectangle {
    id: root
    property bool on: false
    property string text: ""
    property bool bare: false
    property color tint: on ? Theme.ok : Theme.dim

    Layout.minimumWidth: 0
    implicitHeight: bare ? 14 : 24
    implicitWidth: dot.width + Theme.space1 + probe.implicitWidth + (bare ? 0 : 2 * Theme.space15)
    color: bare ? "transparent" : (on ? Theme.ground : Theme.rule2)

    Text { id: probe; visible: false; text: root.text; textFormat: Text.PlainText; font: label.font }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: root.bare ? 0 : Theme.space15
        anchors.rightMargin: root.bare ? 0 : Theme.space15
        spacing: Theme.space1
        Rectangle { id: dot; implicitWidth: root.bare ? 5 : 6; implicitHeight: implicitWidth; color: root.tint }
        Text {
            id: label
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: root.text
            textFormat: Text.PlainText
            elide: Text.ElideRight
            font.pixelSize: root.bare ? Theme.roles.meta11.size : Theme.roles.small.size
            font.weight: root.bare ? Font.Normal : Font.Medium
            color: root.tint
        }
    }
}
