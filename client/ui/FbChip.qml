import QtQuick

// Filter / category chip (tokens.md): 32 high, radius 16. Optional counter badge (count >= 0).
Rectangle {
    id: root
    property string text: ""
    property int count: -1
    property bool badgeAccent: false  // counter badge in accent (needs attention) instead of neutral
    property bool active: false
    signal clicked()

    implicitHeight: 32
    implicitWidth: row.implicitWidth + 28
    radius: Theme.radius16
    color: active ? Theme.text : (hover.hovered ? Theme.surfaceRaised : "transparent")
    border.width: active ? 0 : 1
    border.color: Theme.borderInput
    Behavior on color { ColorAnimation { duration: Theme.durFast } }

    Row {
        id: row
        anchors.centerIn: parent
        Text {
            anchors.verticalCenter: parent.verticalCenter
            // "All · 10": the count is part of the label, once (round 3).
            text: root.count >= 0 ? root.text + " · " + root.count : root.text
            font.pixelSize: Theme.fontSmall
            font.weight: root.active ? Font.Medium : Font.Normal
            color: root.active ? Theme.bg : (root.badgeAccent && root.count > 0 ? Theme.accent : Theme.textSecondary)
        }
    }
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: root.clicked() }
}
