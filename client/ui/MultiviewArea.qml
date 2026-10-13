import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// 3r-3 / 3i-3: Multiview with up to four surfaces (ADR 0012 D8, ADR 0026 D2): the local game and/or remote Sessions. Layouts:
// a single surface fills the area; Row (two or three Sessions side by side, equal columns); Grid 2 x 2 (from four Sessions,
// never an empty slot); PiP: one main tile and the others as windows inside the main tile (a corner of the picture, S | M | L,
// dragged to snap to a corner). Area padding 16, tile gap 12. A tile is surface #121315, radius 10, padding 10, border 2 px:
// the selected tile has the accent border, all others #24252a (the one selection style). Under the picture an info row: index
// chip, name and the pills "♪ Audio" (the one audible surface) and "Paused". A tile is selected by click or the keys 1-4; the
// side panel follows (a remote selection does not route input: input always goes to your game). A paused remote game is dimmed
// and says "Paused by <name>" (ADR 0026 D3). The screen layout of the game applies to every tile. Every surface is one delegate
// that is only moved when the layout changes, so the local game view is not recreated. The "Running sessions" picker lives in
// the game screen.
Item {
    id: root
    objectName: "multiviewArea"
    required property PlayerController player
    readonly property SessionController ctl: player.sessions
    readonly property string layoutMode: ctl.surfaceCount < 2 ? "single" : ctl.multiviewMode   // single | pip | row | grid
    readonly property var order: ctl.surfaceOrder             // display order, first = main surface
    readonly property int count: ctl.surfaceCount
    readonly property bool tiled: layoutMode === "row" || layoutMode === "grid"
    readonly property string selected: ctl.selectedSurface
    signal localReady(Item view)
    signal escapePressed()
    signal pickerRequested()
    signal tileSelected()

    // Layout metrics
    readonly property real pad: 16
    readonly property real gap: 12
    readonly property int cols: layoutMode === "row" ? Math.max(1, count) : 2
    readonly property real cellW: Math.max(0, (width - 2 * pad - (cols - 1) * gap) / cols)
    readonly property real cellH: layoutMode === "grid" ? Math.max(0, (height - 2 * pad - gap) / 2) : Math.max(0, height - 2 * pad)
    // PiP (3i-3): the window sits inside the main tile, 12 px clear of its 2 px border; corner and size are the user's
    property string pipCorner: "br"            // "br" | "bl" | "tr" | "tl"
    property string pipSize: "M"               // "S" | "M" | "L"
    property bool pipDragging: false
    readonly property real pipInset: pad + 2 + 12
    readonly property real pipWidth: pipSize === "S" ? 176 : pipSize === "L" ? 320 : 240
    readonly property real pipChrome: 70       // controls row + caption row around the picture
    readonly property real pipSpacing: 8
    readonly property int pipTiles: Math.max(1, count - 1)
    readonly property real pipViewHeight: Math.max(60, Math.min(pipWidth * 1.35, (height - 2 * pipInset - pipTiles * pipChrome - (pipTiles - 1) * pipSpacing) / pipTiles))
    readonly property real pipTileHeight: pipChrome + pipViewHeight
    function snapCorner(cx, cy) { return (cy < height / 2 ? "t" : "b") + (cx < width / 2 ? "l" : "r") }

    // Plugin tile strips: the visible remote tiles go to the plugins (fb "tiles" event); the strips come back per tile.
    readonly property var remoteTiles: {
        if (!root.visible) return []
        var o = root.ctl.surfaceOrder, info = root.ctl.surfaceInfo, r = []
        for (var i = 0; i < o.length; ++i) {
            if (o[i] === "local") continue
            var si = info[o[i]] || ({})
            r.push({ id: o[i], player: si.who || "", game: si.game || "", user_id: si.userId || "" })
        }
        return r
    }
    onRemoteTilesChanged: root.player.plugins.setTiles(root.remoteTiles)
    property var stripChoice: ({})                 // tile id -> chosen plugin id (several plugins offer a strip)
    PluginMenu {
        id: stripMenu
        property string tileId: ""
        onCloseRequested: open = false
        onPicked: (id) => {
            open = false
            var c = Object.assign({}, root.stripChoice)
            c[stripMenu.tileId] = id
            root.stripChoice = c
        }
    }

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


    // Corner targets while a PiP window is dragged (3i-3): dashed 60 x 40 areas in the corners it is not in
    Repeater {
        model: root.layoutMode === "pip" && root.pipDragging ? ["tl", "tr", "bl", "br"] : []
        delegate: Rectangle {
            required property string modelData
            objectName: "pipSnap_" + modelData
            visible: modelData !== root.pipCorner
            x: modelData.charAt(1) === "l" ? root.pipInset : root.width - root.pipInset - width
            y: modelData.charAt(0) === "t" ? root.pipInset : root.height - root.pipInset - height
            width: 60; height: 40; radius: 6
            color: "transparent"
            border.width: 1
            border.color: "#3a3b40"
            z: 1
        }
    }
    FbLabel {
        objectName: "pipDragHint"
        visible: root.layoutMode === "pip" && root.pipDragging
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.pad + 6
        z: 3
        text: qsTr("Drag to snap to a corner · S / M / L size")
        font.pixelSize: 12
        color: Theme.gameMeta
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
            readonly property bool multiple: root.count > 1
            readonly property bool localPaused: isLocal && root.player.gameSession.paused
            readonly property bool remotePaused: !isLocal && info.paused === true
            readonly property bool paused: localPaused || remotePaused
            readonly property bool loading: !isLocal && info.loading === true && !remotePaused
            readonly property real lane: stripLoader.active ? 24 : 0     // row above the picture for the plugin strip
            property real dragDx: 0
            property real dragDy: 0
            readonly property bool atTop: root.pipCorner.charAt(0) === "t"
            readonly property bool atLeft: root.pipCorner.charAt(1) === "l"

            objectName: "surfaceTile_" + modelData
            visible: slot >= 0
            z: pipTile ? 2 : 0
            width: root.tiled ? root.cellW : pipTile ? root.pipWidth : root.width - 2 * root.pad
            height: root.tiled ? root.cellH : pipTile ? root.pipTileHeight : root.height - 2 * root.pad
            x: (root.layoutMode === "row" ? root.pad + Math.max(0, slot) * (root.cellW + root.gap)
              : root.layoutMode === "grid" ? root.pad + (Math.max(0, slot) % 2) * (root.cellW + root.gap)
              : pipTile ? (atLeft ? root.pipInset : root.width - root.pipInset - root.pipWidth)
              : root.pad) + dragDx
            y: (root.layoutMode === "grid" ? root.pad + Math.floor(Math.max(0, slot) / 2) * (root.cellH + root.gap)
              : pipTile ? (atTop ? root.pipInset + (slot - 1) * (root.pipTileHeight + root.pipSpacing)
                               : root.height - root.pipInset - slot * root.pipTileHeight - (slot - 1) * root.pipSpacing)
              : root.pad) + dragDy

            Rectangle {
                id: surfaceBg
                anchors.fill: parent
                radius: 10
                color: tapHover.hovered && !tile.pipTile ? "#16171a" : (tile.pipTile ? "#17181b" : Theme.gameTileBody)
                border.width: tile.multiple ? 2 : 0
                border.color: tile.pipTile ? Theme.gamePopoverBorder : Theme.gameTileEdge
            }
            HoverHandler { id: tapHover; cursorShape: Qt.PointingHandCursor }
            TapHandler {
                objectName: "tileTap_" + tile.modelData
                onTapped: { root.ctl.selectSurface(tile.modelData); root.tileSelected() }
            }
            DragHandler {
                id: pipDrag
                enabled: tile.pipTile
                target: null
                onTranslationChanged: { tile.dragDx = translation.x; tile.dragDy = translation.y }
                onActiveChanged: {
                    root.pipDragging = active
                    if (!active) {
                        root.pipCorner = root.snapCorner(tile.x + tile.width / 2, tile.y + tile.height / 2)
                        tile.dragDx = 0
                        tile.dragDy = 0
                    }
                }
            }

            // The picture: local game or the decoded frames of this remote Session.
            Item {
                id: pictureBox
                objectName: "tilePicture_" + tile.modelData
                anchors.fill: parent
                anchors.leftMargin: tile.pipTile ? 10 : 12
                anchors.rightMargin: tile.pipTile ? 10 : 12
                anchors.topMargin: (tile.pipTile ? 38 : 12) + tile.lane
                anchors.bottomMargin: tile.pipTile ? 32 : 12 + 26 + 8
                Loader {
                    anchors.fill: parent
                    opacity: tile.remotePaused || tile.loading ? 0.45 : 1
                    sourceComponent: tile.isLocal ? localComp : remoteComp
                }
                // Paused / loading states (3r-3): remote paused by the host, your own paused game, a Session still connecting
                TileStateCard {
                    objectName: "tilePausedBanner"
                    visible: tile.paused
                    anchors.centerIn: parent
                    compact: tile.pipTile
                    title: tile.remotePaused ? qsTr("❚❚ Paused by %1").arg(tile.info.pausedBy || tile.info.who || "") : qsTr("❚❚ Paused")
                    sub: tile.remotePaused ? qsTr("Frozen picture · resumes here") : qsTr("Esc or Resume continues")
                }
                TileStateCard {
                    objectName: "tileLoadingCard"
                    visible: tile.loading
                    anchors.centerIn: parent
                    compact: tile.pipTile
                    title: qsTr("%1’s game is loading").arg(tile.info.who || "")
                    sub: qsTr("Connecting…")
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

            // Selection: the tile's own border turns accent (the one selection style)
            Rectangle {
                objectName: "tileSelection_" + tile.modelData
                visible: tile.picked && tile.multiple
                anchors.fill: parent
                radius: 10
                color: "transparent"
                border.width: 2
                border.color: Theme.gameAccent
            }

            // Plugin tile strip (remote tiles only): 20 high in its own row above the picture, so it never covers the game image
            property var stripList: []
            function refreshStrips() { tile.stripList = tile.isLocal ? [] : root.player.plugins.tileStrips(tile.modelData) }
            Component.onCompleted: refreshStrips()
            Connections {
                target: root.player.plugins
                function onTileStripsChanged() { tile.refreshStrips() }
            }
            readonly property var strip: {
                var l = tile.stripList, want = root.stripChoice[tile.modelData]
                for (var i = 0; i < l.length; ++i) if (l[i].pluginId === want) return l[i]
                return l.length > 0 ? l[0] : null
            }
            Loader {
                id: stripLoader
                active: !tile.isLocal && tile.strip !== null
                x: tile.pipTile ? 10 : 12
                y: tile.pipTile ? 38 : 12
                z: 6
                sourceComponent: PluginTileStrip {
                    player: root.player
                    tileId: tile.modelData
                    strip: tile.strip
                    maxWidth: Math.max(60, tile.width - stripLoader.x - 12 - (tile.stripList.length > 1 ? 24 : 0))
                }
            }
            Rectangle {
                id: stripMore
                objectName: "tileStripMore_" + tile.modelData
                visible: stripLoader.active && tile.stripList.length > 1 && (tapHover.hovered || moreHover.hovered)
                x: stripLoader.x + (stripLoader.item ? stripLoader.item.width : 0) + 4
                y: stripLoader.y
                z: 6
                width: 20; height: 20; radius: 5
                color: moreHover.hovered ? Theme.gameSegmentOn : Theme.gameChip
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Choose the strip")
                Text { anchors.centerIn: parent; text: "⋯"; font.pixelSize: 14; color: Theme.gameText }
                HoverHandler { id: moreHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: {
                        var items = []
                        for (var i = 0; i < tile.stripList.length; ++i)
                            items.push({ id: tile.stripList[i].pluginId, text: tile.stripList[i].name || tile.stripList[i].pluginId,
                                         checked: tile.strip && tile.strip.pluginId === tile.stripList[i].pluginId })
                        stripMenu.tileId = tile.modelData
                        stripMenu.items = items
                        stripMenu.menuWidth = 220
                        stripMenu.anchorItem = stripMore
                        stripMenu.open = true
                    }
                }
            }

            // Info row under the picture: index chip, name, pills (not in a PiP window, which has its own rows)
            RowLayout {
                objectName: "tileInfoRow_" + tile.modelData
                visible: !tile.pipTile
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 12
                height: 26
                spacing: 8
                Rectangle {
                    objectName: "keyBadge_" + tile.modelData
                    visible: tile.multiple
                    Layout.preferredWidth: 22; Layout.preferredHeight: 20; radius: 4
                    color: Theme.gameBadge
                    FbMono { anchors.centerIn: parent; text: String(tile.slot + 1); font.pixelSize: 11; color: Theme.gameBadgeText }
                }
                FbLabel {
                    id: chipText
                    objectName: "nameChip_" + tile.modelData
                    visible: tile.multiple || !tile.isLocal
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    elide: Text.ElideRight
                    text: tile.info.name || ""
                    font.pixelSize: 13
                    color: Theme.gameText
                }
                Item { visible: !chipText.visible; Layout.fillWidth: true }
                Rectangle {
                    objectName: "audioChip_" + tile.modelData
                    visible: tile.audible && tile.multiple
                    Layout.preferredWidth: audioText.implicitWidth + 16
                    Layout.preferredHeight: 20
                    radius: 10
                    color: Theme.gameOnBg
                    FbLabel { id: audioText; anchors.centerIn: parent; text: tile.width < 300 ? qsTr("♪") : qsTr("♪ Audio"); font.pixelSize: 11; font.weight: Font.DemiBold; color: Theme.gameAccent }
                }
                Rectangle {
                    objectName: "pausedPill_" + tile.modelData
                    visible: tile.paused
                    Layout.preferredWidth: pausedText.implicitWidth + 16
                    Layout.preferredHeight: 20
                    radius: 10
                    color: Theme.gameBadge
                    FbLabel { id: pausedText; anchors.centerIn: parent; text: qsTr("Paused"); font.pixelSize: 11; color: "#d0d0d4" }
                }
            }

            // PiP window (3i-3): controls row (index chip, size steps, audio, swap, remove) and the caption
            RowLayout {
                visible: tile.pipTile
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 8
                anchors.topMargin: 10
                height: 24
                spacing: 4
                Rectangle {
                    objectName: "pipKeyBadge_" + tile.modelData
                    visible: tile.pipTile
                    Layout.preferredWidth: 22; Layout.preferredHeight: 20; radius: 4
                    color: Theme.gameBadge
                    FbMono { anchors.centerIn: parent; text: String(tile.slot + 1); font.pixelSize: 11; color: Theme.gameBadgeText }
                }
                Row {
                    spacing: 2
                    Repeater {
                        model: ["S", "M", "L"]
                        delegate: Rectangle {
                            required property string modelData
                            objectName: "pipSize_" + modelData + "_" + tile.modelData
                            width: 20; height: 20; radius: 4
                            color: root.pipSize === modelData ? Theme.gameOnBg : "transparent"
                            FbMono { anchors.centerIn: parent; text: parent.modelData; font.pixelSize: 10; color: root.pipSize === parent.modelData ? Theme.gameAccent : Theme.gameTextMuted }
                            TapHandler { onTapped: root.pipSize = parent.modelData }
                        }
                    }
                }
                Rectangle {
                    objectName: "pipAudioChip_" + tile.modelData
                    visible: tile.audible && tile.multiple
                    Layout.preferredWidth: 20; Layout.preferredHeight: 20; radius: 10
                    color: Theme.gameOnBg
                    FbLabel { anchors.centerIn: parent; text: "♪"; font.pixelSize: 11; font.weight: Font.DemiBold; color: Theme.gameAccent }
                }
                Item { Layout.fillWidth: true }
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
            FbLabel {
                visible: tile.pipTile
                objectName: "pipCaption_" + tile.modelData
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 10
                anchors.bottomMargin: 8
                elide: Text.ElideRight
                text: tile.info.name || ""
                font.pixelSize: 11
                color: Theme.gameText
            }
        }
    }
}
