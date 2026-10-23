import QtQuick

// Focus and activation shell of the interactive plugin blocks: Tab stop, Enter / Space / click activate, the standard
// focus ring of the design system (UzFocusRing) only for keyboard or controller focus, never after a click.
Item {
    id: root
    property bool dark: true
    property bool actionable: true
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

    UzFocusRing { visible: root.ring; z: 100 }

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
