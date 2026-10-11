import QtQuick

// Focus and activation shell of the interactive plugin blocks: Tab stop, Enter / Space / click activate, the standard
// focus ring (2 px gap in the block surface, 2 px accent) only for keyboard or controller focus, never after a click.
Item {
    id: root
    property bool dark: true
    property bool actionable: true
    property int ringRadius: 6
    property color ringGap: Theme.blockColor("surface", dark)
    readonly property bool hovered: hover.hovered && actionable
    readonly property bool pressed: tap.pressed && actionable
    readonly property bool ring: activeFocus && keyboardFocus
    property bool keyboardFocus: false
    property bool mouseFocusing: false
    default property alias content: holder.data
    signal activated()

    activeFocusOnTab: actionable
    implicitWidth: holder.childrenRect.width
    implicitHeight: holder.childrenRect.height

    Item { id: holder; anchors.fill: parent }

    Rectangle {
        visible: root.ring
        z: 100
        anchors.fill: parent
        anchors.margins: -2
        radius: root.ringRadius + 2
        color: "transparent"
        border.width: 2
        border.color: root.ringGap
    }
    Rectangle {
        visible: root.ring
        z: 101
        anchors.fill: parent
        anchors.margins: -4
        radius: root.ringRadius + 4
        color: "transparent"
        border.width: 2
        border.color: Theme.blockColor("accent", root.dark)
    }

    HoverHandler { id: hover; enabled: root.actionable; cursorShape: Qt.PointingHandCursor }
    TapHandler {
        id: tap
        enabled: root.actionable
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: {
            root.mouseFocusing = true
            root.forceActiveFocus()
            root.mouseFocusing = false
            root.activated()
        }
    }
    onActiveFocusChanged: keyboardFocus = activeFocus && !mouseFocusing
    Keys.onPressed: (event) => {
        if (root.actionable && !event.isAutoRepeat && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space)) {
            root.keyboardFocus = true
            root.activated()
            event.accepted = true
        }
    }
}
