import QtQuick
import FrameBeam.Player

// Virtualized game grid for the Library. It lives inside the Library's Flickable (`scroller`) and only keeps the GridView
// window around the visible part alive: the GridView is as tall as the viewport (plus a buffer) and is moved down with
// the scroll position, so delegates are created and recycled for the visible rows only, however large the Library is.
// The host reserves the full content height, so the scroll bar and the layout around it behave as with a plain grid.
Item {
    id: host
    required property Flickable scroller
    required property int cellWidth
    required property int cellHeight
    property alias model: view.model
    property bool showAdded: false
    property string selectedId: ""
    property string runningId: ""
    readonly property alias count: view.count
    property int createdCount: 0   // tiles instantiated so far (tests: stays small for large libraries)
    readonly property int alive: view.contentItem.children.length
    property int buffer: cellHeight
    signal tileClicked(string gameId)

    // Top of this grid in the coordinates of the scroller's content; re-evaluated when the layout above moves.
    readonly property real topInContent: {
        host.y; host.parent ? host.parent.y : 0; host.scroller.contentHeight; host.height
        return host.mapToItem(host.scroller.contentItem, 0, 0).y
    }
    readonly property real fullHeight: view.contentHeight
    readonly property real windowHeight: Math.min(fullHeight, host.scroller.height + 2 * buffer)
    readonly property real windowTop: Math.max(0, Math.min(fullHeight - windowHeight, host.scroller.contentY - topInContent - buffer))

    implicitHeight: fullHeight

    // The tile that currently exists for this game, else null (tests).
    function tileFor(id) {
        const items = view.contentItem.children
        for (let i = 0; i < items.length; ++i) {
            if (items[i].gameId === id) return items[i]
        }
        return null
    }

    function syncWindow() {
        if (view.contentY !== windowTop) view.contentY = windowTop
    }
    onWindowTopChanged: syncWindow()
    onWindowHeightChanged: syncWindow()

    GridView {
        id: view
        y: host.windowTop
        width: host.width
        height: host.windowHeight
        interactive: false
        currentIndex: -1   // no current item that would stay alive outside the window
        clip: false
        cacheBuffer: 0
        cellWidth: host.cellWidth
        cellHeight: host.cellHeight
        add: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durList } }
        displaced: Transition { NumberAnimation { properties: "x,y"; duration: Theme.durList; easing.type: Easing.OutCubic } }
        onContentYChanged: host.syncWindow()
        onContentHeightChanged: host.syncWindow()
        delegate: GameTile {
            width: view.cellWidth
            height: view.cellHeight
            showAdded: host.showAdded
            selected: gameId === host.selectedId
            running: host.runningId !== "" && host.runningId === gameId
            onClicked: host.tileClicked(gameId)
            Component.onCompleted: host.createdCount++
        }
    }
}
