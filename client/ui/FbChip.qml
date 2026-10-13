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
    property real maxWidth: 100000   // the text elides rather than the chip getting wider than this (very wide fonts)
    implicitWidth: Math.min(maxWidth, row.implicitWidth + 28)
    radius: Theme.radius16
    color: active ? Theme.text : (hover.hovered ? Theme.surfaceRaised : "transparent")
    border.width: active ? 0 : 1
    border.color: Theme.borderInput
    Behavior on color { ColorAnimation { duration: Theme.durFast } }

    Row {
        id: row
        anchors.centerIn: parent
        Text {
            width: Math.min(implicitWidth, Math.max(0, root.maxWidth - 28))
            elide: Text.ElideRight
            anchors.verticalCenter: parent.verticalCenter
            // "All · 10": the count is part of the label, once (round 3).
            text: root.count >= 0 ? root.text + " · " + root.count : root.text
            font.pixelSize: Theme.fontSmall
            font.weight: root.active ? Font.Medium : Font.Normal
            color: root.active ? Theme.bg : (root.badgeAccent && root.count > 0 ? Theme.accent : Theme.textSecondary)
        }
    }
    // Keyboard (R-P4): Tab reaches the chip, Enter/Space activates it, the focus ring is the accent border.
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: root.count >= 0 ? root.text + " · " + root.count : root.text
    Accessible.checkable: true
    Accessible.checked: root.active
    Accessible.onPressAction: root.clicked()
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            root.clicked()
            event.accepted = true
        }
    }
    Rectangle {
        objectName: "focusRing"
        visible: root.activeFocus
        anchors.fill: parent
        anchors.margins: -2
        radius: root.radius + 2
        color: "transparent"
        border.width: 2
        border.color: Theme.accent
    }
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: root.clicked() }
}
