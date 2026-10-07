import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// 3h / 3i: Multiview with up to four surfaces (ADR 0012 D8): the local game and/or remote Sessions, each remote
// Session with its own viewer. Layouts: a single surface fills the area; two surfaces side-by-side (3h); three or
// four as a 2 x 2 grid; PiP (3i): one main surface and the others as small tiles stacked bottom right. Exactly one
// surface is audible ("Audio here"), "Swap" makes a tile the main surface, "Remove" leaves that Session. Every
// surface is one delegate that is only moved when the layout changes, so the local game view is not recreated.
Item {
    id: root
    objectName: "multiviewArea"
    required property PlayerController player
    readonly property SessionController ctl: player.sessions
    readonly property string layoutMode: ctl.surfaceCount < 2 ? "single" : ctl.multiviewMode   // single | pip | side | grid
    readonly property var order: ctl.surfaceOrder             // display order, first = main surface
    readonly property int count: ctl.surfaceCount
    readonly property bool tiled: layoutMode === "side" || layoutMode === "grid"
    signal localReady(Item view)

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
                border.color: Theme.gameBorder
            }

            // The picture: local game or the decoded frames of this remote Session.
            Item {
                anchors.fill: parent
                anchors.topMargin: tile.headed ? root.headerHeight : (tile.pipTile ? 40 : 0)
                anchors.bottomMargin: tile.pipTile ? 48 : 0
                anchors.leftMargin: tile.pipTile ? 8 : 0
                anchors.rightMargin: tile.pipTile ? 8 : 0
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
                    Component.onCompleted: root.localReady(this)
                    onEscapePressed: root.player.gameSession.togglePause()
                }
            }
            Component {
                id: remoteComp
                RemoteView {
                    objectName: "remoteView"
                    controller: root.ctl
                    surfaceId: tile.modelData
                }
            }

            // Header (side-by-side, grid, a single remote Session): avatar, "who · game", meta, audio, Swap, Remove.
            RowLayout {
                visible: tile.headed
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 12
                spacing: 10
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
                    FbLabel { Layout.fillWidth: true; text: tile.info.name || ""; elide: Text.ElideRight; font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.gameText }
                    FbLabel { Layout.fillWidth: true; text: tile.info.meta || ""; elide: Text.ElideRight; font.pixelSize: 12; color: Theme.gameTextMuted }
                }
                FbButton {
                    objectName: "audioButton_" + tile.modelData
                    implicitHeight: 30
                    focusPolicy: Qt.NoFocus
                    font.pixelSize: 12
                    kind: tile.audible ? "primary" : "outline"
                    text: tile.audible ? qsTr("Audio on") : qsTr("Audio here")
                    enabled: !tile.audible
                    onClicked: root.ctl.audioHere(tile.modelData)
                }
                FbButton {
                    objectName: "swapButton_" + tile.modelData
                    visible: tile.slot > 0
                    implicitHeight: 30
                    kind: "link"
                    focusPolicy: Qt.NoFocus
                    font.pixelSize: 12
                    text: qsTr("Swap")
                    onClicked: root.ctl.makeMain(tile.modelData)
                }
                FbButton {
                    objectName: "removeButton_" + tile.modelData
                    visible: !tile.isLocal
                    implicitHeight: 30
                    kind: "link"
                    focusPolicy: Qt.NoFocus
                    font.pixelSize: 12
                    text: qsTr("Remove")
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
                    text: tile.audible ? qsTr("Audio on") : qsTr("Audio here")
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
                    text: tile.audible ? qsTr("Audio on") : qsTr("Audio here")
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

    // Session list: "Add" per joinable Session, next to what is already shown (up to four surfaces). Open by
    // default while there is only one surface, otherwise behind the "Add Session" button.
    property bool pickerToggled: false
    property bool pickerWanted: false
    readonly property bool pickerOpen: pickerToggled ? pickerWanted : count <= 1

    FbButton {
        objectName: "addSessionToggle"
        visible: root.count >= 2
        z: 5
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 12
        implicitHeight: 30
        focusPolicy: Qt.NoFocus
        font.pixelSize: 12
        text: root.pickerOpen ? qsTr("Hide Sessions") : qsTr("Add Session")
        onClicked: { root.pickerToggled = true; root.pickerWanted = !root.pickerOpen }
    }

    Rectangle {
        id: pick
        objectName: "multiviewSessionList"
        readonly property var list: root.ctl.sessions
        readonly property var shown: root.ctl.shownSessionIds
        visible: root.pickerOpen
        z: 5
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: 12
        anchors.bottomMargin: root.count >= 2 ? 54 : 14
        width: Math.min(440, parent.width - 24)
        height: pickCol.implicitHeight + 24
        radius: 10
        color: Theme.bgPanel
        border.width: 1
        border.color: Theme.gameBorder
        ColumnLayout {
            id: pickCol
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8
            FbLabel {
                objectName: "multiviewListTitle"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                elide: Text.ElideRight
                text: root.count === 0 ? qsTr("Watch a Session")
                    : root.count >= root.ctl.maxSurfaces ? qsTr("Multiview is full · %1 surfaces").arg(root.ctl.maxSurfaces)
                    : qsTr("Add a Session · %1 of %2 surfaces").arg(root.count).arg(root.ctl.maxSurfaces)
                font.pixelSize: 12
                font.weight: Font.DemiBold
                color: Theme.gameText
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
                spacing: 8
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
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 0
                        spacing: 0
                        FbLabel { Layout.fillWidth: true; elide: Text.ElideRight; text: prow.modelData.title; font.pixelSize: 13; color: Theme.gameText }
                        FbLabel {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: prow.shown ? qsTr("shown · %1").arg(prow.modelData.meta) : prow.modelData.meta
                            font.pixelSize: 11
                            color: prow.modelData.invited && !prow.shown ? Theme.accent : Theme.gameTextMuted
                        }
                    }
                    FbButton {
                        objectName: "multiviewAddButton_" + prow.modelData.sessionId
                        implicitHeight: 30
                        kind: "primary"
                        focusPolicy: Qt.NoFocus
                        font.pixelSize: 12
                        text: qsTr("Add")
                        enabled: root.ctl.hubLink === "online" && root.ctl.canAddSurface && !prow.shown
                        onClicked: root.ctl.watch(prow.modelData.sessionId)
                    }
                }
            }
        }
    }
}
