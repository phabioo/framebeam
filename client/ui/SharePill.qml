import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

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
    radius: 999
    color: bare ? "transparent" : (on ? Theme.gameOkBg : Theme.gameTrack)

    Text { id: probe; visible: false; text: root.text; font: label.font }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: root.bare ? 0 : 10
        anchors.rightMargin: root.bare ? 0 : 10
        spacing: 6
        Rectangle { id: dot; implicitWidth: root.bare ? 5 : 6; implicitHeight: implicitWidth; radius: width / 2; color: root.tint }
        Text {
            id: label
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: root.text
            elide: Text.ElideRight
            font.pixelSize: root.bare ? 11 : 12
            font.weight: root.bare ? Font.Normal : Font.Medium
            color: root.tint
        }
    }
}
