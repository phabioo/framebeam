import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Row of an anchored menu (speed, screen layout, "⋯"): 32 high, hover #2c2d32. glyph/icon left, text, trailing text or check.
Rectangle {
    id: root
    property string text: ""
    property string glyph: ""
    property string iconKind: ""
    property string trailing: ""
    property bool current: false
    property bool divider: false      // top border (separates a group)
    property bool monoText: false
    signal activated()

    implicitHeight: 32 + (divider ? 3 : 0)
    radius: 6
    color: "transparent"
    Accessible.role: Accessible.MenuItem
    Accessible.name: text

    Rectangle { visible: root.divider; anchors.top: parent.top; anchors.topMargin: 1; width: parent.width; height: 1; color: Theme.gameDivider }
    Rectangle {
        anchors.fill: parent
        anchors.topMargin: root.divider ? 3 : 0
        radius: 6
        color: hover.hovered ? Theme.gameSegmentOn : "transparent"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 8
            Item {
                visible: root.glyph !== "" || root.iconKind !== ""
                Layout.preferredWidth: 14
                Layout.preferredHeight: 14
                Text { visible: root.glyph !== ""; anchors.centerIn: parent; text: root.glyph; font.pixelSize: 14; color: Theme.gameText }
                GameIcon { visible: root.iconKind !== ""; anchors.centerIn: parent; kind: root.iconKind; color: root.current ? Theme.gameText : Theme.gameTextMuted }
            }
            FbLabel {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                elide: Text.ElideRight
                text: root.text
                font.pixelSize: 13
                font.family: root.monoText ? Theme.mono : Qt.application.font.family
                font.weight: root.current ? Font.Medium : Font.Normal
                color: root.current ? Theme.gameText : Theme.gameBadgeText
            }
            FbLabel {
                visible: root.trailing !== "" || root.current
                text: root.trailing !== "" ? root.trailing : "✓"
                font.pixelSize: 11
                font.family: Theme.mono
                color: root.trailing !== "" ? Theme.gameFaint : Theme.gameAccent
            }
        }
        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: root.activated() }
    }
}
