import QtQuick

// Focus ring (design-system.md "Focus ring, markers and elevation"). A child of the focused control that fills it; set
// `visible` from the control (visualFocus / activeFocus). Sumi: 2 px gap in the ground colour + 2 px Yamabuki outside.
// Kinari: 2 px Yamabuki + 1 px Sumi outside so the ring reaches 3:1 on paper. `inset` (focus-ring-inset) draws inward, for
// items inside a container that clips an outer ring (menu items, nav items): 2 px Yamabuki, on Kinari with 1 px Sumi inside.
Item {
    id: root
    objectName: "focusRing"
    property bool inset: false
    anchors.fill: parent
    enabled: false   // never takes input
    readonly property int ringWidth: Theme.focusWidth

    // outward: gap (Sumi) | accent ring | ink hairline (Kinari)
    Rectangle {
        visible: !root.inset && Theme.focusGap > 0
        anchors.fill: parent
        anchors.margins: -Theme.focusGap
        color: "transparent"
        border.width: Theme.focusGap
        border.color: Theme.ground
    }
    Rectangle {
        objectName: "focusRingAccent"
        visible: !root.inset
        anchors.fill: parent
        anchors.margins: -(Theme.focusGap + Theme.focusWidth)
        color: "transparent"
        border.width: Theme.focusWidth
        border.color: Theme.accent
    }
    Rectangle {
        visible: !root.inset && Theme.focusOuter > 0
        anchors.fill: parent
        anchors.margins: -(Theme.focusGap + Theme.focusWidth + Theme.focusOuter)
        color: "transparent"
        border.width: Theme.focusOuter
        border.color: Theme.focusOuterColor
    }
    // inset: accent ring inside, ink hairline inside it (Kinari)
    Rectangle {
        visible: root.inset
        anchors.fill: parent
        color: "transparent"
        border.width: Theme.focusWidth
        border.color: Theme.accent
    }
    Rectangle {
        visible: root.inset && Theme.focusOuter > 0
        anchors.fill: parent
        anchors.margins: Theme.focusWidth
        color: "transparent"
        border.width: Theme.focusOuter
        border.color: Theme.focusOuterColor
    }
}
