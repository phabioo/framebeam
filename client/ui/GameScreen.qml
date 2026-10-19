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
// Keys (never forwarded to the core; defaults, configurable in Controllers > Hotkeys except Esc): Esc closes an open popover,
// else always pauses (window and fullscreen, ADR 0026 D1) and shows the pause card; a second Esc or "Resume" continues.
// F11 toggles fullscreen and is the only key that leaves it; F3 diagnostics (docked as a panel tab, in fullscreen the floating
// overlay), F5 snapshot, Space speed-up (only when the core allows it; also while the Session is shared), 1-4 select a tile in
// the Multiview (audio moves with "Audio here").
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
    // The toolbar also stays while the game is paused in fullscreen (3y-2): Resume and Leave fullscreen are in it.
    readonly property bool toolbarShown: topHover.hovered || toolbarPinned || (root.fullscreen && root.session.paused)
    // QWindow::Visibility values (the attached Window type has no enum)
    readonly property int visFullScreen: 5
    readonly property int visWindowed: 2
    property int windowedVisibility: 2
    property bool enteredHere: false

    // 1280 and below: compact header and a 320 px panel
    readonly property bool compact: width < 1400
    // Anchored popover that is open: "" | "reset" | "speed" | "layout" | "picker" | "more" | "tabs". The picker never opens on its own.
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
    readonly property string modeTitle: ctl.multiviewMode === "row" ? qsTr("Multiview · Row")
                                       : ctl.multiviewMode === "grid" ? qsTr("Multiview · Grid 2×2")
                                       : qsTr("Multiview · Picture-in-Picture")

    // In-game plugin screen (fb.ui.open("screen")): plugin id, "" = closed. The game keeps running underneath.
    property string screenSheetPlugin: ""
    function openPluginScreen(pluginId) {
        root.popover = ""
        root.screenSheetPlugin = pluginId
        Qt.callLater(focusLocal)
    }
    function closePluginScreen() {
        if (root.screenSheetPlugin === "") return false
        root.screenSheetPlugin = ""
        Qt.callLater(focusLocal)
        return true
    }
    Connections {
        target: root.player.plugins
        function onOpenRequested(pluginId, slot) {
            if (!root.visible) return
            if (slot === "screen") root.openPluginScreen(pluginId)
            else if (slot === "panel" && !root.fullscreen && !root.multiMode) {
                root.closePluginScreen()
                panel.openTab("plugin:" + pluginId)
            }
        }
    }
    function focusLocal() {
        if (root.screenSheetPlugin !== "") { sheet.forceActiveFocus(); return }
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
    onVisibleChanged: {
        if (visible) {
            Qt.callLater(focusLocal)
        } else {
            root.popover = ""
            root.screenSheetPlugin = ""
            if (root.fullscreen && root.enteredHere) root.setFullscreen(false)   // leaving the game view: the window goes back to where it was
        }
    }
    // A tab picked with the keyboard (focus on a header segment) keeps the focus there; a mouse click hands it back to the game.
    onTabChanged: { root.popover = ""; if (!header.segmentFocused) Qt.callLater(focusLocal) }
    onFullscreenChanged: root.popover = ""

    function setFullscreen(on) {
        if (on === root.fullscreen) return
        var w = root.Window.window
        if (on && w && root.applyToWindow) {
            root.windowedVisibility = w.visibility === root.visFullScreen ? root.visWindowed : w.visibility
            root.enteredHere = true
            w.visibility = root.visFullScreen
        } else if (!on && w && root.applyToWindow && root.enteredHere) {
            root.enteredHere = false
            w.visibility = root.windowedVisibility
        }
        root.fullscreen = on
        if (on) hintTimer.restart()
        Qt.callLater(focusLocal)
    }
    function toggleFullscreen() { setFullscreen(!root.fullscreen) }
    // Esc (ADR 0026 D1): closes an open popover; else it always pauses or resumes your game, in a window and in fullscreen
    // (the pause card opens). F11 is the only key that leaves fullscreen. Watching only: nothing to pause.
    function handleEscape() {
        if (!root.visible) return   // game in the background (Library shown): Esc never acts on it
        if (root.closePluginScreen()) return
        if (root.closePopover()) return
        if (root.session.active) root.session.togglePause()
    }
    // Second line of the pause card: what viewers see (only while shared) and where the game was saved.
    readonly property string pauseDetailText: {
        var parts = []
        if (root.ctl.shared) parts.push(qsTr("Viewers see “Paused by %1”.").arg(root.ctl.ownName !== "" ? root.ctl.ownName : qsTr("the host")))
        var sh = root.player.saveHistory
        if (sh.slotsAvailable && sh.available) {
            var when = sh.history.length > 0 ? sh.history[0].when : ""
            parts.push(qsTr("Saved to “%1”").arg(sh.slotLabel) + (when !== "" ? " · " + when : ""))
        }
        return parts.join(" ")
    }
    function saveSnapshot() {
        if (root.session.active && root.player.saveHistory.available && !root.player.saveHistory.busy)
            root.player.saveHistory.createSnapshotInGame("")
    }

    Connections {
        target: root.Window.window
        function onVisibilityChanged() {
            var w = root.Window.window
            if (w && root.applyToWindow && root.visible) {
                // Number(): comparing the visibility enum directly raised a "Type error" in the compiled binding (Qt 6.4)
                root.fullscreen = Number(w.visibility) === root.visFullScreen
            }
        }
    }

    // Player hotkeys are configurable (Controllers > Hotkeys): the controller maps the Qt key to an action name.
    // Hints show the gamepad combo while a gamepad was the last input used, else the key (name kept: hotkeyLabels).
    readonly property var hotkeyLabels: root.player.controllers.hotkeyHints
    // Runs a hotkey action (keyboard or gamepad); false = unknown action.
    function runHotkey(action) {
        if (action === "fullscreen") root.toggleFullscreen()
        else if (action === "diagnostics") root.diag.toggle()
        else if (action === "snapshot") root.saveSnapshot()
        else if (action === "pause") { if (root.session.active) root.session.togglePause() }
        else if (action === "hud") root.player.plugins.hudAllHidden = !root.player.plugins.hudAllHidden
        else if (action === "speedup") { if (root.session.fastForwardAvailable) root.session.toggleFastForward() }
        else return false
        return true
    }
    Connections {
        target: root.player.controllers
        // Gamepad hotkeys (P1): the buttons are already kept away from the core by the GamepadService.
        function onPadHotkeyTriggered(action) { if (root.visible) root.runHotkey(action) }
    }
    // A text field of a plugin panel has the focus: plugin keys, Alt+n and F4 are typing, not hotkeys.
    function textFieldFocused() {
        var f = root.Window.activeFocusItem
        return !!f && ("cursorPosition" in f)
    }
    // Plugin hotkeys, Alt+1 ... Alt+9 (panel tab n) and F4 (hide or show all HUD widgets); true = the key was used.
    function runExtraHotkey(e) {
        if (root.textFieldFocused()) return false
        if (e.isAutoRepeat) return false
        if (root.player.plugins.triggerHotkeyKey(e.key, e.modifiers)) return true
        var mods = e.modifiers & (Qt.ControlModifier | Qt.ShiftModifier | Qt.AltModifier | Qt.MetaModifier)
        if (mods === Qt.AltModifier && e.key >= Qt.Key_1 && e.key <= Qt.Key_9) {
            if (!root.fullscreen) panel.openTabNumber(e.key - Qt.Key_0)
            return true
        }
        if (e.key === Qt.Key_F4 && mods === Qt.NoModifier) {
            root.player.plugins.hudAllHidden = !root.player.plugins.hudAllHidden
            return true
        }
        return false
    }
    Keys.onPressed: (e) => {
        if (!root.visible) return   // game in the background: hotkeys do not act on it from other screens
        root.player.controllers.noteKeyboardInput()
        if (root.runExtraHotkey(e)) { e.accepted = true; return }
        var action = root.player.controllers.hotkeyAction(e.key)
        if (action !== "") {
            if (!(action === "speedup" && e.isAutoRepeat)) root.runHotkey(action)
            e.accepted = true   // the key never reaches the core (GameSession::isReservedKey)
        } else if (e.key === Qt.Key_Escape) {
            if (!e.isAutoRepeat && !root.textFieldFocused()) root.handleEscape()
            e.accepted = !root.textFieldFocused()
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
                            // With the fullscreen diagnostics overlay open (top left), sit directly below it, left edges aligned.
                            readonly property bool belowDiag: diagnosticsOverlay.visible
                            readonly property point diagBottomLeft: {
                                void parent.width; void parent.height; void diagnosticsOverlay.height;
                                return parent.mapFromItem(diagnosticsOverlay, 0, diagnosticsOverlay.height)
                            }
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.topMargin: belowDiag ? diagBottomLeft.y + 8 : 12
                            anchors.leftMargin: belowDiag ? diagBottomLeft.x : 12
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

                // Pause card (3y-2, ADR 0026 D1): Esc or the Pause buttons pause, the card shows in a window and in fullscreen
                // (in the Multiview window the paused tile says so itself). Resume continues, F11 and "Leave fullscreen" leave.
                Rectangle {
                    id: pauseOverlay
                    objectName: "pauseOverlay"
                    visible: root.session.paused && (root.tab === "session" || root.fullscreen)
                    anchors.fill: parent
                    z: 12
                    color: root.fullscreen ? "#8c000000" : "#73000000"
                    // Absorbs the pointer so the game or a tile underneath is not clicked by accident
                    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; hoverEnabled: true }
                    Rectangle {
                        objectName: "pauseCard"
                        anchors.centerIn: parent
                        width: Math.min(380, parent.width - 32)
                        implicitHeight: pauseCol.implicitHeight + 48
                        height: implicitHeight
                        radius: 14
                        color: Qt.rgba(18 / 255, 19 / 255, 20 / 255, 0.92)
                        border.width: 1
                        border.color: Theme.gameDivider
                        ColumnLayout {
                            id: pauseCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 24
                            anchors.leftMargin: 32
                            anchors.rightMargin: 32
                            spacing: 8
                            FbLabel {
                                objectName: "pauseTitle"
                                Layout.alignment: Qt.AlignHCenter
                                text: qsTr("Paused")
                                font.pixelSize: 22
                                font.weight: Font.DemiBold
                                color: Theme.gameText
                            }
                            FbLabel {
                                objectName: "pauseDetail"
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                text: root.pauseDetailText
                                visible: text !== ""
                                font.pixelSize: 13
                                lineHeight: 1.2
                                color: Theme.gameMeta
                            }
                            // The buttons stack when they do not fit side by side (wide fonts); a single one never exceeds the card.
                            GridLayout {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.topMargin: 8
                                columnSpacing: 10
                                rowSpacing: 8
                                columns: !pauseLeave.visible || pauseResume.implicitWidth + pauseLeave.implicitWidth + columnSpacing <= pauseCol.width ? 2 : 1
                                KeyHintButton {
                                    id: pauseResume
                                    objectName: "pauseResume"
                                    Layout.alignment: Qt.AlignHCenter
                                    maxWidth: pauseCol.width
                                    kind: "primary"
                                    focusPolicy: Qt.NoFocus
                                    text: qsTr("Resume")
                                    onClicked: root.session.togglePause()
                                }
                                KeyHintButton {
                                    id: pauseLeave
                                    objectName: "pauseLeaveFullscreen"
                                    Layout.alignment: Qt.AlignHCenter
                                    maxWidth: pauseCol.width
                                    visible: root.fullscreen
                                    focusPolicy: Qt.NoFocus
                                    text: qsTr("Leave fullscreen")
                                    onClicked: root.setFullscreen(false)
                                }
                            }
                            FbLabel {
                                objectName: "pauseHint"
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                Layout.topMargin: 4
                                text: root.fullscreen ? qsTr("Esc or Resume continues · %1 leaves fullscreen").arg(root.hotkeyLabels.fullscreen || "F11")
                                                      : qsTr("Esc or Resume continues")
                                font.pixelSize: 12
                                color: Theme.gameFaint
                            }
                        }
                    }
                }

                // Diagnostics overlay (3y): only in fullscreen, Emulation, top left. Windowed, the diagnostics are docked as a tab
                // of the side panel (3h-3) so they never cover the game or the remote tiles.
                DiagnosticsOverlay {
                    id: diagnosticsOverlay
                    hotkey: root.hotkeyLabels.diagnostics
                    objectName: "diagnosticsOverlay"
                    x: 16
                    y: 16
                    z: 10
                    visible: root.diag.open && root.fullscreen
                    model: root.diag
                    fullscreen: root.fullscreen
                    multi: false
                    tileBadges: root.tileBadges
                }

                // Plugin HUD widgets and notices (4e, 4g-2); stays in fullscreen, where the panel is hidden
                PluginHudLayer {
                    player: root.player
                    onDetailsRequested: panel.openTab("console")
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
                onSavesClosed: Qt.callLater(root.focusLocal)
                onSelectLocalRequested: root.ctl.selectSurface("local")
                overflowOpen: root.popover === "tabs"
                onOverflowRequested: root.popover = root.popover === "tabs" ? "" : "tabs"
            }
        }
    }

    // In-game plugin screen: covers the play area and the panel below the header
    PluginScreenSheet {
        id: sheet
        player: root.player
        pluginId: root.screenSheetPlugin
        anchors.top: noticeBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        z: 4
        onCloseRequested: root.closePluginScreen()
    }

    // ---- anchored popovers (opened from the header) ----

    AnchoredPopover {
        id: resetPop
        objectName: "resetPopover"
        anchorItem: header.resetAnchor
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
            // Very wide fonts: the header has no room even for the mode cycle button, so the layout choice lives here.
            Repeater {
                model: header.cycleHidden && header.modeAvailable ? header.modeChoices : []
                delegate: PopoverItem {
                    required property var modelData
                    objectName: "moreMode_" + modelData
                    width: parent.width
                    text: modelData === "pip" ? qsTr("PiP") : modelData === "row" ? qsTr("Row") : qsTr("Grid 2×2")
                    current: header.ctl.multiviewMode === modelData
                    onActivated: { header.ctl.multiviewMode = modelData; root.closePopover() }
                }
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

    // "+n ▾": the panel tabs that do not fit. Opens left of the side panel, at the height of the tab bar, so it never covers
    // the sharing section (P41).
    AnchoredPopover {
        id: tabsPop
        objectName: "panelTabOverflowPopover"
        anchorItem: panel.overflowAnchor
        open: root.popover === "tabs" && panel.tabPlan.overflow.length > 0
        popWidth: 250
        padding: 4
        dockRight: panel.visible ? panel.width + 8 : 8
        scrimTop: header.height
        onCloseRequested: root.closePopover()
        Column {
            width: parent.width
            Repeater {
                model: panel.tabPlan.overflow
                delegate: PopoverItem {
                    required property var modelData
                    objectName: "panelTabOverflowItem_" + modelData.id
                    width: parent.width
                    text: modelData.name
                    trailing: panel.originMarker(modelData.origin)
                    onActivated: { root.closePopover(); panel.openTab(modelData.id) }
                }
            }
        }
    }

    AnchoredPopover {
        id: pickerPop
        objectName: "pickerPopover"
        anchorItem: header.addAnchor
        open: root.popover === "picker"
        popWidth: 320
        padding: 12
        dockRight: panel.visible ? panel.width + 16 : 16   // below the header, left of the side panel (P41)
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
                    objectName: "fullscreenPauseButton"
                    visible: root.session.active
                    enabled: root.session.state === GameSession.Running || root.session.state === GameSession.Paused
                    kind: root.session.paused ? "primary" : "outline"
                    focusPolicy: Qt.NoFocus
                    text: root.session.paused ? qsTr("▶ Resume") : qsTr("❚❚ Pause")
                    hint: "Esc"
                    onClicked: root.session.togglePause()
                }
                KeyHintButton {
                    objectName: "exitFullscreenButton"
                    focusPolicy: Qt.NoFocus
                    text: qsTr("Leave fullscreen")
                    hint: root.hotkeyLabels.fullscreen || "F11"
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
