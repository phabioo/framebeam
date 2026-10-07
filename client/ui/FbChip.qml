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
        spacing: 8
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            font.pixelSize: Theme.fontSmall
            font.weight: root.active ? Font.Medium : Font.Normal
            color: root.active ? Theme.bg : Theme.textSecondary
        }
        Rectangle {
            visible: root.count >= 0
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(18, badge.implicitWidth + 10)
            height: 18
            radius: 9
            color: root.badgeAccent && root.count > 0 ? Theme.accentChipBg : (root.active ? Theme.bg : Theme.neutralPillBg)
            Text {
                id: badge
                anchors.centerIn: parent
                text: root.count
                font.pixelSize: Theme.fontMono
                font.weight: Font.DemiBold
                color: root.badgeAccent && root.count > 0 ? Theme.accent : (root.active ? Theme.text : Theme.textMuted)
            }
        }
    }
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: root.clicked() }
}
