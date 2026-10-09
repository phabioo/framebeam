import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// 3r-2 / 3h-2 / 3i-2: Multiview with up to four surfaces (ADR 0012 D8): the local game and/or remote Sessions. Layouts: a
// single surface fills the area; side by side (two columns); grid 2 x 2 (free cells are "+ Add session" tiles); PiP: one
// main surface and the others as windows stacked bottom right. Tiles sit on #0f1011 with radius 8 in a 16 px padding and gap.
// A tile is selected by click on its badge, name or margin (the picture of your own game keeps the mouse for touch) or by
// the keys 1-4; the side panel follows the selection (a remote selection does not route input: input always goes to your
// game). Exactly one surface is audible: inset ring 2 px accent and the chip "♪ Audio" (moved with "Audio here" in the
// panel). The screen layout of the game applies to every tile. Every surface is one delegate that is only moved when the
// layout changes, so the local game view is not recreated. The "Running sessions" picker lives in the game screen.
Item {
    id: root
    objectName: "multiviewArea"
    required property PlayerController player
    readonly property SessionController ctl: player.sessions
    readonly property string layoutMode: ctl.surfaceCount < 2 ? "single" : ctl.multiviewMode   // single | pip | side | grid
    readonly property var order: ctl.surfaceOrder             // display order, first = main surface
    readonly property int count: ctl.surfaceCount
    readonly property bool tiled: layoutMode === "side" || layoutMode === "grid"
    readonly property string selected: ctl.selectedSurface
    signal localReady(Item view)
    signal escapePressed()
    signal pickerRequested()
    signal tileSelected()

    // Layout metrics
    readonly property real pad: 16
    readonly property real gap: 16
    readonly property real cellW: Math.max(0, (width - 2 * pad - gap) / 2)
    readonly property real cellH: Math.max(0, (height - 2 * pad - gap) / 2)
    readonly property real pipMargin: 24
    readonly property real pipWidth: 288
    readonly property real pipChrome: 64       // badge row + name row around the picture
    readonly property real pipSpacing: 8
    readonly property int pipTiles: Math.max(1, count - 1)
    readonly property real pipViewHeight: Math.max(60, Math.min(384, (height - 2 * pipMargin - pipTiles * pipChrome - (pipTiles - 1) * pipSpacing) / pipTiles))
    readonly property real pipTileHeight: pipChrome + pipViewHeight

    // One entry per surface, synced incrementally: adding or removing a Session must not recreate the other
    // surfaces (above all not the local game view).
    ListModel { id: surfaceModel }
    function syncSurfaces() {
        var ids = root.ctl.surfaceIds
        for (var i = surfaceModel.count - 1; i >= 0; --i) {
            if (ids.indexOf(surfaceModel.get(i).sid) < 0) surfaceModel.remove(i)
        }
        for (var j = 0; j < ids.length; ++j) {
            var found = false
            for (var k = 0; k < surfaceModel.count; ++k) {
                if (surfaceModel.get(k).sid === ids[j]) { found = true; break }
            }
            if (!found) surfaceModel.append({ sid: ids[j] })
        }
    }
    Component.onCompleted: syncSurfaces()
    Connections {
        target: root.ctl
        function onSurfacesChanged() { root.syncSurfaces() }
    }

    // Free cells of the grid: "+ Add session" (click opens the picker)
    Repeater {
        model: root.layoutMode === "grid" ? Math.max(0, 4 - root.count) : 0
        delegate: Item {
            id: freeCell
            required property int index
            readonly property int slot: root.count + index
            objectName: "emptyTile_" + (slot + 1)
            x: root.pad + (slot % 2) * (root.cellW + root.gap)
            y: root.pad + Math.floor(slot / 2) * (root.cellH + root.gap)
            width: root.cellW
            height: root.cellH
            Canvas {  // dashed 1.5 px border (a Rectangle border cannot be dashed)
                anchors.fill: parent
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    var c = getContext("2d")
                    c.clearRect(0, 0, width, height)
                    c.strokeStyle = "#2c2d32"
                    c.lineWidth = 1.5
                    c.setLineDash([6, 4])
                    var r = 8, x = 0.75, y = 0.75, w = width - 1.5, h = height - 1.5
                    c.beginPath()
                    c.moveTo(x + r, y)
                    c.arcTo(x + w, y, x + w, y + h, r)
                    c.arcTo(x + w, y + h, x, y + h, r)
                    c.arcTo(x, y + h, x, y, r)
                    c.arcTo(x, y, x + w, y, r)
                    c.closePath()
                    c.stroke()
                }
            }
            Rectangle { anchors.fill: parent; radius: 8; color: addHover.hovered ? Theme.gameTileHover : "transparent" }
            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width - 24
                spacing: 6
                FbLabel { Layout.alignment: Qt.AlignHCenter; text: qsTr("+ Add session"); font.pixelSize: 14; font.weight: Font.Medium; color: Theme.gameBadgeText }
                FbLabel { Layout.alignment: Qt.AlignHCenter; text: qsTr("Opens Running sessions"); font.pixelSize: 12; color: Theme.gameFaint }
            }
            HoverHandler { id: addHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: root.pickerRequested() }
        }
    }

    Repeater {
        model: surfaceModel
        delegate: Item {
            id: tile
            required property string sid
            readonly property string modelData: sid
            readonly property int slot: root.order.indexOf(modelData)
            readonly property var info: root.ctl.surfaceInfo[modelData] || ({})
            readonly property bool isLocal: modelData === "local"
            readonly property bool audible: root.ctl.audioFocus === modelData
            readonly property bool picked: root.selected === modelData
            readonly property bool pipTile: root.layoutMode === "pip" && slot > 0
            readonly property bool pipMain: root.layoutMode === "pip" && slot === 0
            readonly property bool multiple: root.count > 1
            readonly property bool localPaused: isLocal && root.player.gameSession.paused

            objectName: "surfaceTile_" + modelData
            visible: slot >= 0
            z: pipTile ? 2 : 0
            width: root.layoutMode === "side" || root.layoutMode === "grid" ? root.cellW
                 : pipTile ? root.pipWidth : root.width - 2 * root.pad
            height: root.layoutMode === "side" ? root.height - 2 * root.pad
                  : root.layoutMode === "grid" ? root.cellH
                  : pipTile ? root.pipTileHeight : root.height - 2 * root.pad
            x: root.layoutMode === "side" ? root.pad + Math.max(0, slot) * (root.cellW + root.gap)
             : root.layoutMode === "grid" ? root.pad + (Math.max(0, slot) % 2) * (root.cellW + root.gap)
             : pipTile ? root.width - root.pipMargin - root.pipWidth : root.pad
            y: root.layoutMode === "grid" ? root.pad + Math.floor(Math.max(0, slot) / 2) * (root.cellH + root.gap)
             : pipTile ? root.height - root.pipMargin - slot * root.pipTileHeight - (slot - 1) * root.pipSpacing
             : root.pad

            Rectangle {
                id: surfaceBg
                anchors.fill: parent
                radius: 8
                color: tapHover.hovered && !tile.pipTile ? Theme.gameTileHover : Theme.gameTile
                border.width: tile.pipTile ? 1 : 0
                border.color: Theme.gameDivider
            }
            HoverHandler { id: tapHover; cursorShape: Qt.PointingHandCursor }
            TapHandler {
                objectName: "tileTap_" + tile.modelData
                onTapped: { root.ctl.selectSurface(tile.modelData); root.tileSelected() }
            }

            // The picture: local game or the decoded frames of this remote Session.
            Item {
                anchors.fill: parent
                anchors.topMargin: tile.pipTile ? 32 : 36
                anchors.bottomMargin: tile.pipTile ? 32 : 36
                anchors.leftMargin: tile.pipTile ? 8 : 12
                anchors.rightMargin: tile.pipTile ? 8 : 12
                Loader {
                    anchors.fill: parent
                    sourceComponent: tile.isLocal ? localComp : remoteComp
                }
            }
            Component {
                id: localComp
                GameView {
                    session: root.player.gameSession
                    objectName: "gameViewMulti"
                    layout: root.ctl.screenLayout
                    Component.onCompleted: root.localReady(this)
                    onEscapePressed: root.escapePressed()
                }
            }
            Component {
                id: remoteComp
                RemoteView {
                    objectName: "remoteView"
                    controller: root.ctl
                    surfaceId: tile.modelData
                    layout: root.ctl.screenLayout
                }
            }

            // Paused banner on your own tile (3h-2)
            Rectangle {
                objectName: "tilePausedBanner"
                visible: tile.localPaused
                anchors.centerIn: parent
                width: Math.min(parent.width - 24, pausedRow.implicitWidth + 28)
                height: 34
                radius: 8
                color: Qt.rgba(17 / 255, 18 / 255, 20 / 255, 0.9)
                RowLayout {
                    id: pausedRow
                    anchors.centerIn: parent
                    width: parent.width - 28
                    spacing: 8
                    GameIcon { kind: "pause"; color: Theme.gameText }
                    FbLabel { Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight; text: qsTr("Paused · Resume in the header or Esc"); font.pixelSize: 13; font.weight: Font.Medium; color: Theme.gameText }
                }
            }

            // Audio focus: inset ring 2 px accent
            Rectangle {
                objectName: "audioRing_" + tile.modelData
                visible: tile.audible && tile.multiple
                anchors.fill: parent
                radius: 8
                color: "transparent"
                border.width: 2
                border.color: Theme.gameAccent
            }
            // Selection: outline 2 px #ecebe7, offset 3
            Rectangle {
                objectName: "tileSelection_" + tile.modelData
                visible: tile.picked && tile.multiple
                anchors.fill: parent
                anchors.margins: -5
                radius: 11
                color: "transparent"
                border.width: 2
                border.color: Theme.gameSelection
            }

            // Key badge (top left); selected = white
            Rectangle {
                objectName: "keyBadge_" + tile.modelData
                visible: tile.multiple
                x: tile.pipTile ? 8 : 10
                y: tile.pipTile ? 8 : 10
                width: 20; height: 20; radius: 4
                readonly property bool sel: tile.picked
                color: sel ? Theme.gameSelection : Theme.gameBadge
                FbMono { anchors.centerIn: parent; text: String(tile.slot + 1); font.pixelSize: 11; color: parent.sel ? "#121315" : Theme.gameBadgeText }
            }
            // PiP window: swap and remove
            Row {
                visible: tile.pipTile
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 6
                spacing: 4
                GameButton {
                    objectName: "swapButton_" + tile.modelData
                    look: "surface"
                    fixedWidth: 24
                    implicitHeight: 24
                    glyph: "⇄"
                    tip: qsTr("Swap with the main picture")
                    onClicked: root.ctl.makeMain(tile.modelData)
                }
                GameButton {
                    objectName: "removeButton_" + tile.modelData
                    visible: !tile.isLocal
                    look: "surface"
                    fixedWidth: 24
                    implicitHeight: 24
                    glyph: "×"
                    tip: qsTr("Remove from multiview")
                    onClicked: root.ctl.removeSurface(tile.modelData)
                }
            }
            // Name chip (bottom left)
            Rectangle {
                objectName: "nameChip_" + tile.modelData
                visible: tile.multiple || !tile.isLocal
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.leftMargin: tile.pipTile ? 8 : 10
                anchors.bottomMargin: tile.pipTile ? 8 : 10
                width: Math.min(parent.width - (tile.audible && tile.multiple ? 90 : 24), chipText.implicitWidth + 16)
                height: chipText.implicitHeight + 8
                radius: tile.pipTile ? 5 : 6
                color: Theme.gameChip
                FbLabel {
                    id: chipText
                    anchors.centerIn: parent
                    width: parent.width - 16
                    elide: Text.ElideRight
                    textFormat: Text.StyledText
                    text: (tile.info.name || "") + (tile.localPaused ? " · <font color=\"#3cbfd8\">" + qsTr("paused") + "</font>" : "")
                    font.pixelSize: tile.pipTile ? 11 : 12
                    color: Theme.gameText
                }
            }
            // Audio chip (bottom right)
            Rectangle {
                objectName: "audioChip_" + tile.modelData
                visible: tile.audible && tile.multiple
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: tile.pipTile ? 8 : 10
                anchors.bottomMargin: tile.pipTile ? 8 : 10
                width: audioText.implicitWidth + 16
                height: audioText.implicitHeight + 6
                radius: 999
                color: Theme.gameOnBg
                FbLabel {
                    id: audioText
                    anchors.centerIn: parent
                    text: tile.pipTile ? "♪" : qsTr("♪ Audio")
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    color: Theme.gameAccent
                }
            }
        }
    }
}
