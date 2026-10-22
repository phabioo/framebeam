import QtQuick
import Uzume.App

// Anchored popover (design system "Popover": surface plane, 1 px rule frame, no pointer, no shadow): a panel 10 px below the
// anchor; click outside (below `scrimTop`) closes. Placed as a child of the item it is positioned in
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
    // Docked placement (3r-3, P41): `dockRight` >= 0 puts the panel `dockRight` px from the right edge, right under `scrimTop`, without a
    // pointer, so it opens in the play area and never covers the side panel (header, tabs, sharing).
    property real dockRight: -1
    readonly property bool docked: dockRight >= 0
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
        x: root.docked ? Math.max(8, root.width - root.dockRight - root.popWidth) : Math.max(8, Math.min(root.width - root.popWidth - 8, wantedX))
        y: root.docked ? root.scrimTop + 4 : root.ay + root.ah + 10
        width: root.popWidth
        height: bodyBox.height

        Rectangle {
            id: bodyBox
            width: parent.width
            height: body.childrenRect.height + 2 * root.padding
            radius: 0
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
