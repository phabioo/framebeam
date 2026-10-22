import QtQuick
import QtQuick.Layouts
import Uzume.App

// Share state of the game header and panel: dot + text. on = shared (green), else muted. bare = no pill background (11).
Rectangle {
    id: root
    property bool on: false
    property string text: ""
    property bool bare: false
    property color tint: on ? Theme.gameOk : Theme.gameMeta

    Layout.minimumWidth: 0
    implicitHeight: bare ? 14 : 24
    implicitWidth: dot.width + 6 + probe.implicitWidth + (bare ? 0 : 20)
    radius: 0
    color: bare ? "transparent" : (on ? Theme.gameOkBg : Theme.gameTrack)

    Text { id: probe; visible: false; text: root.text; textFormat: Text.PlainText; font: label.font }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: root.bare ? 0 : 10
        anchors.rightMargin: root.bare ? 0 : 10
        spacing: 6
        Rectangle { id: dot; implicitWidth: root.bare ? 5 : 6; implicitHeight: implicitWidth; radius: 0; color: root.tint }
        Text {
            id: label
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: root.text
            textFormat: Text.PlainText
            elide: Text.ElideRight
            font.pixelSize: root.bare ? 11 : 12
            font.weight: root.bare ? Font.Normal : Font.Medium
            color: root.tint
        }
    }
}
