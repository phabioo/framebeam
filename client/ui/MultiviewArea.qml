import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// 3h / 3r / 3i: Multiview with up to four surfaces (ADR 0012 D8): the local game and/or remote Sessions, each remote
// Session with its own viewer. Layouts: a single surface fills the area; side-by-side (3h, two columns); grid 2 x 2 (3r,
// free cells are "Add a session" tiles); PiP (3i): one main surface and the others as small tiles stacked bottom right.
// Exactly one surface is audible, the one with the accent ring (keys 1-4 in the grid, "Audio here"). The screen layout
// of the game (Stacked / Side by side / Top only) applies to every tile. Every surface is one delegate that is only moved
// when the layout changes, so the local game view is not recreated.
Item {
    id: root
    objectName: "multiviewArea"
    required property PlayerController player
    readonly property SessionController ctl: player.sessions
    readonly property string layoutMode: ctl.surfaceCount < 2 ? "single" : ctl.multiviewMode   // single | pip | side | grid
    readonly property var order: ctl.surfaceOrder             // display order, first = main surface
    readonly property int count: ctl.surfaceCount
    readonly property bool tiled: layoutMode === "side" || layoutMode === "grid"
    // "Running sessions" picker (3r); the header button of the game view toggles it, an empty tile opens it.
    property bool pickerOpen: false
    signal localReady(Item view)
    signal escapePressed()
    signal pickerRequested()
    signal pickerCloseRequested()

    // Layout metrics
    readonly property real gap: 2
    readonly property real headerHeight: 58    // surface header incl. margins (side, grid, single remote)
    readonly property real pipMargin: 28
    readonly property real pipWidth: 248
    readonly property real pipChrome: 88       // tile padding + header row + buttons around the picture
    readonly property real pipSpacing: 8
    readonly property int pipTiles: Math.max(1, count - 1)
    readonly property real pipViewHeight: Math.max(60, Math.min(300, (height - 2 * pipMargin - pipTiles * pipChrome - (pipTiles - 1) * pipSpacing) / pipTiles))
    readonly property real pipTileHeight: pipChrome + pipViewHeight

    // Layout divider (side-by-side and grid)
    Rectangle {
        anchors.fill: parent
        color: root.tiled ? Theme.gameBorder : Theme.gameBg
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

    // Free cells of the grid: "Add a session" (click opens the picker)
    Repeater {
        model: root.layoutMode === "grid" ? Math.max(0, 4 - root.count) : 0
        delegate: Rectangle {
            id: freeCell
            required property int index
            readonly property int slot: root.count + index
            readonly property real cellW: (root.width - root.gap) / 2
            readonly property real cellH: (root.height - root.gap) / 2
            objectName: "emptyTile_" + (slot + 1)
            x: (slot % 2) * (cellW + root.gap)
            y: Math.floor(slot / 2) * (cellH + root.gap)
            width: cellW
            height: cellH
            color: Theme.gameBg
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 6
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: 44
                    implicitHeight: 44
                    radius: 22
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.borderButton
                    FbLabel { anchors.centerIn: parent; text: "+"; font.pixelSize: 22; color: Theme.textMuted }
                }
                FbLabel { Layout.alignment: Qt.AlignHCenter; text: qsTr("Add a session"); font.pixelSize: 14; color: Theme.text }
                FbLabel { Layout.alignment: Qt.AlignHCenter; text: qsTr("Tile %1 is free").arg(freeCell.slot + 1); font.pixelSize: 12; color: Theme.textTile }
            }
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
            readonly property var link: root.ctl.surfaceLinks[modelData] || null
            readonly property bool isLocal: modelData === "local"
            readonly property bool audible: root.ctl.audioFocus === modelData
            readonly property bool pipTile: root.layoutMode === "pip" && slot > 0
            readonly property bool pipMain: root.layoutMode === "pip" && slot === 0
            readonly property bool headed: root.tiled || (root.layoutMode === "single" && !isLocal)
            readonly property real cellW: (root.width - root.gap) / 2
            readonly property real cellH: (root.height - root.gap) / 2

            objectName: "surfaceTile_" + modelData
            visible: slot >= 0
            z: pipTile ? 2 : 0
            width: root.layoutMode === "side" ? cellW
                 : root.layoutMode === "grid" ? cellW
                 : pipTile ? root.pipWidth : root.width
            height: root.layoutMode === "side" ? root.height
                  : root.layoutMode === "grid" ? cellH
                  : pipTile ? root.pipTileHeight : root.height
            x: root.layoutMode === "side" ? Math.max(0, slot) * (cellW + root.gap)
             : root.layoutMode === "grid" ? (Math.max(0, slot) % 2) * (cellW + root.gap)
             : pipTile ? root.width - root.pipMargin - root.pipWidth : 0
            y: root.layoutMode === "grid" ? Math.floor(Math.max(0, slot) / 2) * (cellH + root.gap)
             : pipTile ? root.height - root.pipMargin - slot * root.pipTileHeight - (slot - 1) * root.pipSpacing
             : 0

            Rectangle {
                anchors.fill: parent
                color: tile.pipTile ? Theme.bgPanel : Theme.gameBg
                radius: tile.pipTile ? 10 : 0
                border.width: tile.pipTile ? 1 : 0
                border.color: Theme.borderCard
            }

            // The picture: local game or the decoded frames of this remote Session.
            Item {
                anchors.fill: parent
                anchors.topMargin: tile.headed ? root.headerHeight : (tile.pipTile ? 40 : 0)
                anchors.bottomMargin: tile.pipTile ? 48 : (tile.headed ? 12 : 0)
                anchors.leftMargin: tile.pipTile ? 8 : (tile.headed ? 20 : 0)
                anchors.rightMargin: tile.pipTile ? 8 : (tile.headed ? 20 : 0)
                clip: tile.pipTile
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

            // Audio focus ring (side, grid): inset 2 px accent
            Rectangle {
                objectName: "audioRing_" + tile.modelData
                visible: tile.audible && root.tiled
                anchors.fill: parent
                color: "transparent"
                border.width: 2
                border.color: Theme.accent
            }

            // Header (side-by-side, grid, a single remote Session): key badge (grid), avatar, "who · game" with the
            // connection pill, audio button, remove.
            RowLayout {
                visible: tile.headed
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.topMargin: 14
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 10
                Rectangle {
                    objectName: "keyBadge_" + tile.modelData
                    visible: root.layoutMode === "grid"
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    radius: 4
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.borderButton
                    FbMono { anchors.centerIn: parent; text: String(tile.slot + 1); font.pixelSize: 11; color: Theme.textMuted }
                }
                Rectangle {
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    radius: 14
                    color: Theme.surfaceRaised
                    FbLabel {
                        anchors.centerIn: parent
                        text: ((tile.info.name || "?").charAt(0)).toUpperCase()
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        color: Theme.gameText
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 0
                    spacing: 0
                    FbLabel { Layout.fillWidth: true; text: tile.info.name || ""; elide: Text.ElideRight; font.pixelSize: 14; font.weight: Font.Medium; color: Theme.gameText }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        FbLabel { Layout.fillWidth: true; Layout.minimumWidth: 0; text: tile.info.meta || ""; elide: Text.ElideRight; font.pixelSize: 12; color: Theme.textMeta }
                        FbPill {
                            objectName: "tilePill_" + tile.modelData
                            visible: !tile.isLocal && tile.link !== null
                            small: true
                            text: tile.link ? tile.link.text : ""
                            tone: tile.link ? tile.link.tone : "neutral"
                        }
                    }
                }
                FbButton {
                    objectName: "audioButton_" + tile.modelData
                    implicitHeight: 30
                    focusPolicy: Qt.NoFocus
                    font.pixelSize: 12
                    kind: tile.audible ? "primary" : "outline"
                    text: tile.audible ? qsTr("Audio active") : qsTr("Audio here")
                    enabled: !tile.audible
                    onClicked: root.ctl.audioHere(tile.modelData)
                }
                FbButton {
                    objectName: "removeButton_" + tile.modelData
                    visible: !tile.isLocal
                    implicitWidth: 28
                    implicitHeight: 28
                    kind: "outline"
                    focusPolicy: Qt.NoFocus
                    font.pixelSize: 14
                    text: "×"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Remove from multiview")
                    onClicked: root.ctl.removeSurface(tile.modelData)
                }
            }

            // PiP main surface: audio and Remove (a remote Session can be the main surface after "Swap").
            RowLayout {
                visible: tile.pipMain
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: 12
                spacing: 8
                FbButton {
                    objectName: "audioButton_" + tile.modelData
                    implicitHeight: 28
                    focusPolicy: Qt.NoFocus
                    font.pixelSize: 12
                    kind: tile.audible ? "primary" : "outline"
                    text: tile.audible ? qsTr("Audio active") : qsTr("Audio here")
                    enabled: !tile.audible
                    onClicked: root.ctl.audioHere(tile.modelData)
                }
                FbButton {
                    objectName: "removeButton_" + tile.modelData
                    visible: !tile.isLocal
                    implicitHeight: 28
                    focusPolicy: Qt.NoFocus
                    font.pixelSize: 12
                    text: qsTr("Remove")
                    onClicked: root.ctl.removeSurface(tile.modelData)
                }
            }

            // PiP tile (3i): status dot, "who · game", audio; buttons "Swap" and "Remove".
            RowLayout {
                visible: tile.pipTile
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 8
                height: 24
                spacing: 8
                StatusDot { tone: "ok" }
                FbLabel {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: tile.info.name || ""
                    elide: Text.ElideRight
                    font.pixelSize: 12
                    color: Theme.gameText
                }
                FbButton {
                    objectName: "audioButton_" + tile.modelData
                    implicitHeight: 24
                    focusPolicy: Qt.NoFocus
                    font.pixelSize: 11
                    kind: tile.audible ? "primary" : "outline"
                    text: tile.audible ? qsTr("Audio active") : qsTr("Audio here")
                    enabled: !tile.audible
                    onClicked: root.ctl.audioHere(tile.modelData)
                }
            }
            RowLayout {
                visible: tile.pipTile
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 8
                spacing: 8
                FbButton {
                    objectName: "swapButton_" + tile.modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: 32
                    focusPolicy: Qt.NoFocus
                    font.pixelSize: 13
                    text: qsTr("Swap")
                    onClicked: root.ctl.makeMain(tile.modelData)
                }
                FbButton {
                    objectName: "removeButton_" + tile.modelData
                    visible: !tile.isLocal
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: 32
                    focusPolicy: Qt.NoFocus
                    font.pixelSize: 13
                    text: qsTr("Remove")
                    onClicked: root.ctl.removeSurface(tile.modelData)
                }
            }
        }
    }

    // "Running sessions" picker (3r): Add / ✓ Added · Remove / disabled when full.
    Rectangle {
        id: pick
        objectName: "multiviewSessionList"
        readonly property var list: root.ctl.sessions
        readonly property var shown: root.ctl.shownSessionIds
        readonly property bool full: root.count >= root.ctl.maxSurfaces
        visible: root.pickerOpen
        z: 5
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 8
        anchors.rightMargin: 20
        width: Math.min(400, parent.width - 24)
        height: pickCol.implicitHeight + 28
        radius: 10
        color: Theme.popupBg
        border.width: 1
        border.color: Theme.borderPopup

        ColumnLayout {
            id: pickCol
            anchors.fill: parent
            anchors.margins: 14
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                FbLabel {
                    objectName: "multiviewListTitle"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    elide: Text.ElideRight
                    text: qsTr("Running sessions")
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: Theme.gameText
                }
                FbMono { objectName: "multiviewListCount"; text: qsTr("%1 of %2 tiles").arg(root.count).arg(root.ctl.maxSurfaces); font.pixelSize: 12; color: Theme.textMeta }
                FbButton {
                    objectName: "multiviewListClose"
                    kind: "link"
                    focusPolicy: Qt.NoFocus
                    font.pixelSize: 12
                    text: qsTr("Close")
                    onClicked: root.pickerCloseRequested()
                }
            }
            FbLabel {
                objectName: "multiviewNoSessions"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                visible: pick.list.length === 0
                wrapMode: Text.WordWrap
                text: qsTr("No other Sessions on this Hub right now")
                font.pixelSize: 12
                color: Theme.gameTextMuted
            }
            ListView {
                id: pickView
                objectName: "multiviewSessionView"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                visible: pick.list.length > 0
                // All Sessions, scrollable once the list outgrows half of the game area.
                Layout.preferredHeight: Math.min(contentHeight, Math.max(120, root.height * 0.5))
                clip: true
                spacing: 10
                boundsBehavior: Flickable.StopAtBounds
                model: pick.list
                ScrollBar.vertical: ScrollBar { policy: pickView.contentHeight > pickView.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
                delegate: RowLayout {
                    id: prow
                    required property var modelData
                    required property int index
                    readonly property bool shown: pick.shown.indexOf(modelData.sessionId) >= 0
                    objectName: "multiviewSession_" + modelData.sessionId
                    width: ListView.view.width - (pickView.contentHeight > pickView.height ? 12 : 0)
                    spacing: 10
                    Rectangle {
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        radius: 14
                        color: Theme.surfaceRaised
                        FbLabel {
                            anchors.centerIn: parent
                            text: ((prow.modelData.title || "?").charAt(0)).toUpperCase()
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            color: Theme.gameText
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 0
                        spacing: 0
                        FbLabel { Layout.fillWidth: true; elide: Text.ElideRight; text: prow.modelData.title; font.pixelSize: 13; color: Theme.gameText }
                        FbLabel {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: prow.modelData.meta
                            font.pixelSize: 12
                            color: prow.modelData.invited && !prow.shown ? Theme.accent : Theme.textMeta
                        }
                    }
                    FbButton {
                        objectName: "multiviewAddButton_" + prow.modelData.sessionId
                        visible: !prow.shown
                        implicitHeight: 30
                        implicitWidth: 64
                        kind: pick.full ? "outline" : "primary"
                        focusPolicy: Qt.NoFocus
                        font.pixelSize: 12
                        text: qsTr("Add")
                        enabled: root.ctl.hubLink === "online" && root.ctl.canAddSurface && !prow.shown
                        background: Rectangle {
                            radius: 6
                            color: parent.enabled ? Theme.accent : Theme.surfaceRaised
                        }
                        onClicked: root.ctl.watch(prow.modelData.sessionId)
                    }
                    FbButton {
                        objectName: "multiviewRemoveButton_" + prow.modelData.sessionId
                        visible: prow.shown
                        implicitHeight: 30
                        kind: "link"
                        focusPolicy: Qt.NoFocus
                        font.pixelSize: 12
                        text: qsTr("✓ Added · Remove")
                        onClicked: root.ctl.removeSurface(prow.modelData.sessionId)
                    }
                }
            }
            FbLabel {
                objectName: "multiviewListNote"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                wrapMode: Text.WordWrap
                text: pick.full ? qsTr("Multiview is full. Remove a tile to add another session.")
                                : qsTr("Sessions you may watch. Private sessions are not listed. Sound plays from one tile only.")
                font.pixelSize: 12
                color: Theme.textMeta
            }
        }
    }
}
