import QtQuick
import FrameBeam.Player

// Anchored popover (docs/design/player.md "Anchored popover"): a 12 px rotated-square pointer centered on the anchor and a
// panel 10 px below it; click outside (below `scrimTop`) closes. Placed as a child of the item it is positioned in
// (the game screen); the anchor stays "pressed" through the owner's `on` state. Esc is handled by the game screen (it
// closes the open popover before it pauses).
Item {
    id: root
    property Item anchorItem: null
    property bool open: false
    property bool danger: false
    property real popWidth: 290
    property real scrimTop: 0
    property string align: "center"       // "center" | "right" (right edge 60 beyond the anchor)
    property real padding: 14
    default property alias content: body.data
    readonly property real contentHeight: body.childrenRect.height
    signal closeRequested()

    z: 50
    visible: open && anchorItem !== null
    anchors.fill: parent

    property real ax: 0
    property real ay: 0
    property real aw: 0
    property real ah: 0
    function reposition() {
        if (!anchorItem || !parent) return
        const p = anchorItem.mapToItem(root, 0, 0)
        ax = p.x; ay = p.y; aw = anchorItem.width; ah = anchorItem.height
    }
    onOpenChanged: if (open) reposition()
    onWidthChanged: reposition()
    onHeightChanged: reposition()
    Timer { running: root.open; interval: 120; repeat: true; onTriggered: root.reposition() }

    MouseArea {
        objectName: "popoverScrim"
        x: 0
        y: root.scrimTop
        width: root.width
        height: Math.max(0, root.height - root.scrimTop)
        acceptedButtons: Qt.AllButtons
        onPressed: (m) => { m.accepted = true; root.closeRequested() }
    }

    Item {
        id: panel
        objectName: "popoverPanel"
        readonly property real wantedX: root.align === "right" ? root.ax + root.aw + 60 - root.popWidth
                                                               : root.ax + root.aw / 2 - root.popWidth / 2
        x: Math.max(8, Math.min(root.width - root.popWidth - 8, wantedX))
        y: root.ay + root.ah + 10
        width: root.popWidth
        height: bodyBox.height

        Rectangle {  // pointer: only its upper two edges show, the lower half is covered by the panel
            width: 12
            height: 12
            rotation: 45
            x: Math.max(14, Math.min(panel.width - 26, root.ax + root.aw / 2 - panel.x - 6))
            y: -6
            color: Theme.gamePopover
            border.width: 1
            border.color: root.danger ? Theme.gameDangerBorder : Theme.gamePopoverBorder
        }
        Rectangle {  // soft shadow
            x: 0
            y: 10
            width: parent.width
            height: parent.height
            radius: 12
            color: "#33000000"
            z: -1
        }
        Rectangle {
            id: bodyBox
            width: parent.width
            height: body.childrenRect.height + 2 * root.padding
            radius: 10
            color: Theme.gamePopover
            border.width: 1
            border.color: root.danger ? Theme.gameDangerBorder : Theme.gamePopoverBorder
            Item {
                id: body
                x: root.padding
                y: root.padding
                width: parent.width - 2 * root.padding
                height: childrenRect.height
            }
        }
    }
}
