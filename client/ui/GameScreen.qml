import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// Game view (3g-2, 3r-2, 3h-2, 3i-2, 3t-2, 3x-2): GameHeader (56 px, five zones) over the play area and the GamePanel
// (340, 320 at 1280, or a 48 px rail). Session = your game; Multiview = MultiviewArea (tiles), the panel follows the
// selected tile. Diagnostics: one overlay toggled by the header button and F3, top right of the play area. The anchored
// popovers (Reset, speed, screen layout, Running sessions, "⋯") are opened from the header and live here so they can
// overlap header and play area. Fullscreen (F11) drops header and panel; the toolbar appears when the mouse moves to the
// top. Without a local game (watching only) the remote Session fills the surface.
// Keys (never forwarded to the core; defaults, configurable in Controllers > Hotkeys except Esc): F11 fullscreen, Esc closes
// an open popover, else leaves fullscreen, else pauses; F3 diagnostics, F5 snapshot, Space speed-up (only when the core
// allows it; also while the Session is shared), 1-4 select a tile in the Multiview (audio moves with "Audio here").
Rectangle {
    id: root
    required property PlayerController player
    readonly property GameSession session: player.gameSession
    readonly property SessionController ctl: player.sessions
    readonly property DiagnosticsModel diag: ctl.diagnostics
    readonly property string tab: ctl.tab
    property Item multiLocal: null
    color: Theme.gameBg

    // Fullscreen mirrors the window; applyToWindow = false keeps tests and screenshots from resizing it.
    property bool fullscreen: false
    property bool applyToWindow: true
    property bool toolbarPinned: false      // tests/screenshots: toolbar shown without the mouse at the top
    readonly property bool toolbarShown: topHover.hovered || toolbarPinned
    property int windowedVisibility: Window.Windowed
    property bool enteredHere: false

    // 1280 and below: compact header and a 320 px panel
    readonly property bool compact: width < 1400
    // Anchored popover that is open: "" | "reset" | "speed" | "layout" | "picker" | "more". The picker never opens on its own.
    property string popover: ""
    property bool panelCollapsed: false
    readonly property bool multiMode: tab !== "session"
    readonly property var tileBadges: {
        var m = {}, o = ctl.surfaceOrder
        for (var i = 0; i < o.length; ++i) m[o[i]] = i + 1
        return m
    }

    // Layouts of the running system (D12); remote pictures offer the switch for frames with two stacked screens.
    readonly property var screenLayouts: session.active ? session.screenLayouts : (ctl.watching ? ["stacked", "side", "top"] : [])
    readonly property string modeTitle: ctl.multiviewMode === "side" ? qsTr("Multiview · Side by side")
                                       : ctl.multiviewMode === "grid" ? qsTr("Multiview · Grid 2×2")
                                       : qsTr("Multiview · Picture-in-Picture")

    function focusLocal() {
        var v = root.tab === "session" ? view : root.multiLocal
        if (v) v.forceActiveFocus()
    }
    function togglePopover(name) {
        root.popover = root.popover === name ? "" : name
        if (root.popover === "") Qt.callLater(focusLocal)
    }
    function closePopover() {
        if (root.popover === "") return false
        root.popover = ""
        Qt.callLater(focusLocal)
        return true
    }
    function manageSaves() {
        // Back to the Library with the game paused in the background and its entry selected (the saves view of the
        // Library follows with the Library package).
        root.player.leaveGameView()
        var id = root.player.backgroundGame.id
        if (id !== undefined) root.player.selectGame(id)
    }
    onVisibleChanged: {
        if (visible) {
            Qt.callLater(focusLocal)
        } else {
            root.popover = ""
            if (root.fullscreen && root.enteredHere) root.setFullscreen(false)   // leaving the game view: the window goes back to where it was
        }
    }
    onTabChanged: { root.popover = ""; Qt.callLater(focusLocal) }
    onFullscreenChanged: root.popover = ""

    function setFullscreen(on) {
        if (on === root.fullscreen) return
        var w = root.Window.window
        if (on && w && root.applyToWindow) {
            root.windowedVisibility = w.visibility === Window.FullScreen ? Window.Windowed : w.visibility
            root.enteredHere = true
            w.visibility = Window.FullScreen
        } else if (!on && w && root.applyToWindow && root.enteredHere) {
            root.enteredHere = false
            w.visibility = root.windowedVisibility
        }
        root.fullscreen = on
        if (on) hintTimer.restart()
        Qt.callLater(focusLocal)
    }
    function toggleFullscreen() { setFullscreen(!root.fullscreen) }
    // Esc: closes an open popover; else leaves fullscreen; else it keeps its meaning (pause)
    function handleEscape() {
        if (!root.visible) return   // game in the background (Library shown): Esc never acts on it
        if (root.closePopover()) return
        if (root.fullscreen) setFullscreen(false)
        else root.session.togglePause()
    }
    function saveSnapshot() {
        if (root.session.active && root.player.saveHistory.available && !root.player.saveHistory.busy)
            root.player.saveHistory.createSnapshotInGame("")
    }

    Connections {
        target: root.Window.window
        function onVisibilityChanged() {
            var w = root.Window.window
            if (w && root.applyToWindow && root.visible) root.fullscreen = (w.visibility === Window.FullScreen)
        }
    }

    // Player hotkeys are configurable (Controllers > Hotkeys): the controller maps the Qt key to an action name.
    readonly property var hotkeyLabels: root.player.controllers.hotkeyLabels
    Keys.onPressed: (e) => {
        if (!root.visible) return   // game in the background: hotkeys do not act on it from other screens
        var action = root.player.controllers.hotkeyAction(e.key)
        if (action === "fullscreen") {
            root.toggleFullscreen()
            e.accepted = true
        } else if (action === "diagnostics") {
            root.diag.toggle()
            e.accepted = true
        } else if (action === "snapshot") {
            root.saveSnapshot()
            e.accepted = true
        } else if (action === "speedup") {
            if (!e.isAutoRepeat && root.session.fastForwardAvailable) root.session.toggleFastForward()
            e.accepted = true   // the key never reaches the core (GameSession::isReservedKey)
        } else if (e.key === Qt.Key_Escape) {
            if (root.closePopover()) { e.accepted = true }
            else if (root.fullscreen) { root.setFullscreen(false); e.accepted = true }
        } else if (e.key >= Qt.Key_1 && e.key <= Qt.Key_4 && root.multiMode) {
            var id = root.ctl.surfaceOrder[e.key - Qt.Key_1]
            if (id !== undefined) root.ctl.selectSurface(id)
            e.accepted = true
        }
    }

    Timer { id: hintTimer; interval: 6000 }

    GameHeader {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        z: 5
        visible: !root.fullscreen
        height: visible ? 56 : 0
        player: root.player
        compact: root.compact
        popover: root.popover
        hotkeys: root.hotkeyLabels
        onPopoverToggled: (name) => root.togglePopover(name)
        onFullscreenRequested: root.toggleFullscreen()
    }

    // Non-blocking notice: the save of the running game changed on another device
    Rectangle {
        id: noticeBar
        objectName: "saveNotice"
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: root.player.saveHistory.notice !== ""
        height: visible ? Math.max(36, noticeText.implicitHeight + 16) : 0
        color: Theme.accentChipBg
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 12
            FbLabel {
                id: noticeText
                Layout.fillWidth: true
                text: root.player.saveHistory.notice
                wrapMode: Text.WordWrap
                font.pixelSize: 13
                color: Theme.accent
            }
            FbButton { kind: "link"; focusPolicy: Qt.NoFocus; text: qsTr("Dismiss"); onClicked: root.player.saveHistory.dismissNotice() }
        }
    }

    // Play area and panel
    Item {
        id: body
        anchors.top: noticeBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        RowLayout {
            anchors.fill: parent
            spacing: 0

            Item {
                id: playArea
                objectName: "playArea"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 0

                // Session tab: your game
                Item {
                    objectName: "sessionTab"
                    anchors.fill: parent
                    visible: root.tab === "session"

                    GameView {
                        id: view
                        objectName: "gameView"
                        anchors.fill: parent
                        session: root.session
                        layout: root.ctl.screenLayout
                        focus: true
                        onEscapePressed: root.handleEscape()

                        FbLabel {
                            anchors.centerIn: parent
                            visible: !root.session.hasFrame && root.session.state !== GameSession.Failed
                            text: qsTr("Starting emulator…")
                            color: Theme.gameTextMuted
                        }
                        Rectangle {
                            objectName: "fastForwardIndicator"
                            visible: root.session.fastForward && !root.session.paused
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.margins: 12
                            width: ffLabel.implicitWidth + 20
                            height: ffLabel.implicitHeight + 10
                            radius: 6
                            color: "#99000000"
                            FbLabel {
                                id: ffLabel
                                anchors.centerIn: parent
                                text: qsTr("Speed-up ×%1").arg(Math.round(root.session.fastForwardRatio * 10) / 10)
                                color: Theme.gameText
                                font.pixelSize: 13
                                font.weight: Font.Medium
                            }
                        }
                        Rectangle {
                            visible: root.session.paused
                            anchors.fill: parent
                            color: "#99000000"
                            FbLabel {
                                anchors.centerIn: parent
                                text: qsTr("Paused · Esc or Resume")
                                color: Theme.gameText
                                font.pixelSize: 18
                                font.weight: Font.Medium
                            }
                        }
                    }
                }

                // Multiview
                ColumnLayout {
                    objectName: "multiviewTab"
                    anchors.fill: parent
                    visible: root.tab !== "session"
                    spacing: 0

                    Rectangle {
                        objectName: "gameMessage"
                        Layout.fillWidth: true
                        visible: root.ctl.message !== ""
                        implicitHeight: 34
                        color: root.ctl.messageIsError ? Theme.errorBg : Theme.surfaceRaised
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 20
                            anchors.rightMargin: 12
                            FbLabel {
                                Layout.fillWidth: true
                                text: root.ctl.message
                                color: root.ctl.messageIsError ? Theme.errorText : Theme.gameText
                                font.pixelSize: 13
                            }
                            FbButton { kind: "link"; focusPolicy: Qt.NoFocus; text: qsTr("Dismiss"); onClicked: root.ctl.dismissMessage() }
                        }
                    }
                    FbLabel {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.margins: 20
                        visible: !root.session.active && !root.ctl.watching
                        text: root.ctl.joining ? qsTr("Joining Session…") : qsTr("Not watching a Session.")
                        color: Theme.gameTextMuted
                    }
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        MultiviewArea {
                            anchors.fill: parent
                            player: root.player
                            onLocalReady: (v) => { root.multiLocal = v; Qt.callLater(root.focusLocal) }
                            onPickerRequested: root.popover = "picker"
                            onTileSelected: Qt.callLater(root.focusLocal)
                            onEscapePressed: root.handleEscape()
                        }
                    }
                }

                // Diagnostics overlay (3t-2, 3x-2): top right of the play area, 16 px under the header and 16 px left of
                // the panel, in every view. Fullscreen (3y): Emulation only, top left.
                DiagnosticsOverlay {
                    hotkey: root.hotkeyLabels.diagnostics
                    objectName: "diagnosticsOverlay"
                    x: root.fullscreen ? 16 : parent.width - width - 16
                    y: 16
                    z: 10
                    visible: root.diag.open
                    model: root.diag
                    fullscreen: root.fullscreen
                    multi: root.multiMode && !root.fullscreen
                    tileBadges: root.tileBadges
                }
            }

            GamePanel {
                id: panel
                visible: !root.fullscreen
                Layout.fillHeight: true
                Layout.preferredWidth: visible ? implicitWidth : 0
                player: root.player
                compact: root.compact
                collapsed: root.panelCollapsed
                multi: root.multiMode
                tile: root.multiMode ? root.ctl.selectedSurface : "local"
                onCollapseToggled: root.panelCollapsed = !root.panelCollapsed
                onManageSavesRequested: root.manageSaves()
                onSelectLocalRequested: root.ctl.selectSurface("local")
            }
        }
    }

    // ---- anchored popovers (opened from the header) ----

    AnchoredPopover {
        id: resetPop
        objectName: "resetPopover"
        anchorItem: root.compact ? header.moreAnchor : header.resetAnchor
        open: root.popover === "reset"
        danger: true
        popWidth: 290
        scrimTop: header.height
        onCloseRequested: root.closePopover()
        Column {
            width: parent.width
            spacing: 10
            Column {
                width: parent.width
                spacing: 4
                FbLabel {
                    objectName: "resetTitle"
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: qsTr("Reset %1?").arg(root.session.title)
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: Theme.gameText
                }
                FbLabel {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    lineHeight: 1.2
                    text: qsTr("Unsaved progress since the last checkpoint is lost.")
                    font.pixelSize: 12
                    color: Theme.gameBadgeText
                }
            }
            Row {
                layoutDirection: Qt.RightToLeft
                width: parent.width
                spacing: 8
                Rectangle {
                    objectName: "resetConfirm"
                    width: confirmLabel.implicitWidth + 24
                    height: 30
                    radius: 6
                    color: Theme.gameDanger
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Reset")
                    FbLabel { id: confirmLabel; anchors.centerIn: parent; text: qsTr("Reset"); font.pixelSize: 13; font.weight: Font.DemiBold; color: "#161512" }
                    TapHandler { onTapped: { root.session.reset(); root.closePopover() } }
                }
                Rectangle {
                    objectName: "resetCancel"
                    width: cancelLabel.implicitWidth + 24
                    height: 30
                    radius: 6
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.gameKeyLine
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Cancel")
                    FbLabel { id: cancelLabel; anchors.centerIn: parent; text: qsTr("Cancel"); font.pixelSize: 13; color: Theme.gameText }
                    TapHandler { onTapped: root.closePopover() }
                }
            }
        }
    }

    AnchoredPopover {
        id: speedPop
        objectName: "speedPopover"
        anchorItem: header.speedAnchor
        open: root.popover === "speed"
        popWidth: 150
        padding: 4
        scrimTop: header.height
        onCloseRequested: root.closePopover()
        Column {
            width: parent.width
            Repeater {
                model: root.session.speedUpRatios
                delegate: PopoverItem {
                    required property var modelData
                    objectName: "speedOption_" + modelData
                    width: parent.width
                    monoText: true
                    text: modelData + "×"
                    current: Math.abs(root.session.fastForwardRatio - Number(modelData)) < 0.001
                    onActivated: { root.session.fastForwardRatio = Number(modelData); root.closePopover() }
                }
            }
        }
    }

    AnchoredPopover {
        id: layoutPop
        objectName: "layoutPopover"
        anchorItem: header.layoutAnchor
        open: root.popover === "layout"
        popWidth: 190
        padding: 4
        scrimTop: header.height
        onCloseRequested: root.closePopover()
        Column {
            width: parent.width
            Repeater {
                model: root.screenLayouts
                delegate: PopoverItem {
                    required property string modelData
                    objectName: modelData === "stacked" ? "layoutStacked" : modelData === "side" ? "layoutSide" : "layoutTop"
                    width: parent.width
                    text: modelData === "stacked" ? qsTr("Stacked") : modelData === "side" ? qsTr("Side by side") : qsTr("Top only")
                    iconKind: "layout"
                    current: root.ctl.screenLayout === modelData
                    onActivated: { root.ctl.screenLayout = modelData; root.closePopover() }
                }
            }
            FbLabel {
                width: parent.width
                topPadding: 4
                bottomPadding: 6
                leftPadding: 10
                text: root.multiMode ? qsTr("Applies to all tiles") : qsTr("This game only")
                font.pixelSize: 11
                color: Theme.gameFaint
            }
        }
    }

    AnchoredPopover {
        id: morePop
        objectName: "morePopover"
        anchorItem: header.moreAnchor
        open: root.popover === "more"
        popWidth: 250
        padding: 4
        scrimTop: header.height
        onCloseRequested: root.closePopover()
        Column {
            width: parent.width
            PopoverItem {
                objectName: "moreReset"
                width: parent.width
                glyph: "↺"
                text: qsTr("Reset %1…").arg(root.session.title)
                onActivated: root.popover = "reset"
            }
            PopoverItem {
                objectName: "morePause"
                width: parent.width
                text: root.session.paused ? qsTr("Resume") : qsTr("Pause")
                trailing: "Esc"
                onActivated: { root.session.togglePause(); root.closePopover() }
            }
            PopoverItem {
                objectName: "moreHotkeys"
                width: parent.width
                divider: true
                text: qsTr("Hotkeys…")
                trailing: qsTr("Controllers")
                onActivated: { root.popover = ""; root.player.leaveGameView(); root.player.showControllers() }
            }
        }
    }

    AnchoredPopover {
        id: pickerPop
        objectName: "pickerPopover"
        anchorItem: header.addAnchor
        open: root.popover === "picker"
        align: "right"
        popWidth: 340
        padding: 12
        scrimTop: header.height
        onCloseRequested: root.closePopover()
        SessionPicker {
            width: parent.width
            player: root.player
            maxListHeight: Math.max(120, root.height * 0.5 - 60)
        }
    }

    // ---- fullscreen furniture (3g, 3y) ----

    // Closed overlay in fullscreen: small chip "F3 Diagnostics" (3y)
    Rectangle {
        objectName: "diagChip"
        visible: root.fullscreen && !root.diag.open
        x: 16
        y: 16
        z: 10
        implicitWidth: chipRow.implicitWidth + 20
        implicitHeight: 28
        radius: 7
        color: Qt.rgba(17 / 255, 18 / 255, 20 / 255, 0.8)
        RowLayout {
            id: chipRow
            anchors.centerIn: parent
            spacing: 8
            Rectangle {
                visible: root.hotkeyLabels.diagnostics !== ""
                implicitWidth: chipKey.implicitWidth + 10
                implicitHeight: 16
                radius: 3
                color: "transparent"
                border.width: 1
                border.color: Theme.borderButton
                FbMono { id: chipKey; anchors.centerIn: parent; text: root.hotkeyLabels.diagnostics; font.pixelSize: 10; color: Theme.textMuted }
            }
            FbLabel { text: qsTr("Diagnostics"); font.pixelSize: 12; color: Theme.textMeta }
        }
        TapHandler { onTapped: root.diag.open = true }
    }

    // Toolbar: appears when the mouse moves to the top
    Item {
        id: topZone
        objectName: "fullscreenTopZone"
        visible: root.fullscreen
        z: 20
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 96
        HoverHandler { id: topHover }

        Rectangle {
            id: toolbar
            objectName: "fullscreenToolbar"
            visible: root.toolbarShown
            anchors.top: parent.top
            anchors.topMargin: 18
            anchors.horizontalCenter: parent.horizontalCenter
            implicitWidth: toolbarRow.implicitWidth + 20
            implicitHeight: 48
            radius: 10
            color: Theme.toolbarBg
            border.width: 1
            border.color: Theme.borderInput

            RowLayout {
                id: toolbarRow
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 6
                spacing: 12
                FbLabel {
                    objectName: "fullscreenToolbarTitle"
                    text: root.tab === "session" ? (root.session.active ? root.session.title : qsTr("Session")) : root.modeTitle
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    color: Theme.gameText
                }
                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: Theme.gameBorder }
                LayoutSwitch {
                    objectName: "toolbarLayoutSwitch"
                    layouts: root.screenLayouts
                    current: root.ctl.screenLayout
                    onPicked: (v) => root.ctl.screenLayout = v
                }
                KeyHintButton {
                    objectName: "exitFullscreenButton"
                    focusPolicy: Qt.NoFocus
                    text: qsTr("Exit fullscreen")
                    hint: root.hotkeyLabels.fullscreen !== "" ? root.hotkeyLabels.fullscreen + " · Esc" : "Esc"
                    onClicked: root.setFullscreen(false)
                }
            }
        }
    }
    FbLabel {
        objectName: "fullscreenHint"
        visible: root.fullscreen && !root.toolbarShown && hintTimer.running
        z: 20
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.hotkeyLabels.diagnostics !== "" ? qsTr("Toolbar appears when you move the mouse to the top · %1 diagnostics").arg(root.hotkeyLabels.diagnostics)
                                                   : qsTr("Toolbar appears when you move the mouse to the top")
        font.pixelSize: 12
        color: Theme.textDisabled
    }
    Rectangle {
        objectName: "fullscreenToast"
        visible: root.fullscreen && root.player.saveHistory.message !== ""
        z: 20
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 56
        anchors.horizontalCenter: parent.horizontalCenter
        implicitWidth: toastText.implicitWidth + 24
        implicitHeight: 32
        radius: 8
        color: Theme.toolbarBg
        FbLabel { id: toastText; anchors.centerIn: parent; text: root.player.saveHistory.message; font.pixelSize: 13; color: Theme.gameText }
    }
}
