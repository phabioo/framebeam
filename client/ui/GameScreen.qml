import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// Game view (3g-3i): header (56 px) with "← Library", title, Session pill, tabs Session | Multiview | Diagnostics, the
// layout switch and Fullscreen; Session tab = local game + side panel (340), Multiview = MultiviewArea (3h, 3r, 3i). The
// Diagnostics tab toggles the overlay (3t-3y; bottom panel in the multiview). Fullscreen (F11) drops header and panel; the
// toolbar appears when the mouse moves to the top. Without a local game (watching only) the remote Session fills the surface.
// Keys (never forwarded to the core): F11 fullscreen, Esc leaves fullscreen (else pause), F3 diagnostics, F5 snapshot,
// 1-4 audio focus in the grid.
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

    // "Running sessions" picker: open by default while there is at most one surface
    property bool pickerToggled: false
    property bool pickerWanted: false
    readonly property bool pickerOpen: pickerToggled ? pickerWanted : ctl.surfaceCount <= 1

    // Layouts of the running system (D12); remote pictures offer the switch for frames with two stacked screens.
    readonly property var screenLayouts: session.active ? session.screenLayouts : (ctl.watching ? ["stacked", "side", "top"] : [])
    readonly property string modeTitle: ctl.multiviewMode === "side" ? qsTr("Multiview · Side-by-Side")
                                       : ctl.multiviewMode === "grid" ? qsTr("Multiview · Grid 2×2")
                                       : qsTr("Multiview · Picture-in-Picture")

    function focusLocal() {
        var v = root.tab === "session" ? view : root.multiLocal
        if (v) v.forceActiveFocus()
    }
    onVisibleChanged: {
        if (visible) {
            Qt.callLater(focusLocal)
        } else if (root.fullscreen && root.enteredHere) {
            root.setFullscreen(false)   // leaving the game view: the window goes back to where it was
        }
    }
    onTabChanged: Qt.callLater(focusLocal)

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
    // Esc: leaves fullscreen when fullscreen, otherwise it keeps its meaning (pause)
    function handleEscape() {
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

    Keys.onPressed: (e) => {
        if (e.key === Qt.Key_F11) {
            root.toggleFullscreen()
            e.accepted = true
        } else if (e.key === Qt.Key_F3) {
            root.diag.toggle()
            e.accepted = true
        } else if (e.key === Qt.Key_F5) {
            root.saveSnapshot()
            e.accepted = true
        } else if (e.key === Qt.Key_Escape) {
            if (root.fullscreen) { root.setFullscreen(false); e.accepted = true }
        } else if (e.key >= Qt.Key_1 && e.key <= Qt.Key_4 && root.tab !== "session" && root.ctl.multiviewMode === "grid") {
            var id = root.ctl.surfaceOrder[e.key - Qt.Key_1]
            if (id !== undefined) root.ctl.audioHere(id)
            e.accepted = true
        }
    }

    Timer { id: hintTimer; interval: 6000 }

    Rectangle {
        id: header
        objectName: "gameHeader"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        visible: !root.fullscreen
        height: visible ? 56 : 0
        color: Theme.gameHeader

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.gameBorder
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            spacing: 10

            FbButton {
                objectName: "backToLibraryButton"
                Layout.minimumWidth: implicitWidth  // never squeezed; the title elides first
                kind: "link"
                implicitHeight: 36
                focusPolicy: Qt.NoFocus
                text: qsTr("← Library")
                onClicked: root.player.leaveGameView()
            }
            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: Theme.gameBorder }
            FbLabel {
                objectName: "gameTitle"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.maximumWidth: 360
                text: root.tab !== "session" && (root.session.active || root.ctl.surfaceCount > 1) ? qsTr("Multiview")
                     : root.session.active ? root.session.title
                     : root.ctl.surfaceCount > 1 ? qsTr("%1 Sessions").arg(root.ctl.surfaceCount)
                     : qsTr("Session from %1").arg(root.ctl.watchedWho)
                color: Theme.gameText
                font.pixelSize: 15
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            FbPill {
                objectName: "sharedPill"
                Layout.minimumWidth: implicitWidth  // never squeezed; the title elides first
                visible: root.ctl.shared
                tone: "ok"
                text: qsTr("● Session shared · %1 watching").arg(root.ctl.viewerCount)
            }
            FbPill {
                objectName: "notSharedPill"
                Layout.minimumWidth: implicitWidth  // never squeezed; the title elides first
                visible: root.session.active && !root.ctl.shared && root.tab === "session"
                tone: "neutral"
                text: qsTr("Not shared")
            }
            FbSegment {
                objectName: "modeSegment"
                Layout.minimumWidth: implicitWidth  // never squeezed; the title elides first
                // PiP | Side-by-Side | Grid 2×2 (side-by-side only while there are two surfaces)
                visible: root.tab !== "session" && root.ctl.availableLayouts.length > 0
                current: root.ctl.multiviewMode
                options: root.ctl.availableLayouts.map(function (m) {
                    return m === "pip" ? { value: "pip", label: qsTr("PiP"), name: "modePip" }
                         : m === "side" ? { value: "side", label: qsTr("Side-by-Side"), name: "modeSide" }
                         : { value: "grid", label: qsTr("Grid 2×2"), name: "modeGrid" }
                })
                onPicked: (v) => root.ctl.multiviewMode = v
            }
            FbMono {
                objectName: "tileCount"
                visible: root.width >= 1700 && root.tab !== "session" && root.ctl.multiviewMode === "grid" && root.ctl.surfaceCount > 1
                text: qsTr("%1 of %2 tiles · keys 1–4 move audio").arg(root.ctl.surfaceCount).arg(root.ctl.maxSurfaces)
                font.pixelSize: 12
                color: Theme.textMeta
            }
            Item { Layout.fillWidth: true }
            FbButton {
                objectName: "pauseButton"
                Layout.minimumWidth: implicitWidth  // never squeezed; the title elides first
                visible: root.session.active && root.tab === "session"
                implicitHeight: 36
                kind: "link"
                focusPolicy: Qt.NoFocus
                text: root.session.paused ? qsTr("Resume") : qsTr("Pause")
                enabled: root.session.state === GameSession.Running || root.session.state === GameSession.Paused
                onClicked: root.session.togglePause()
            }
            FbButton {
                objectName: "resetButton"
                Layout.minimumWidth: implicitWidth  // never squeezed; the title elides first
                visible: root.session.active && root.tab === "session"
                implicitHeight: 36
                kind: "link"
                focusPolicy: Qt.NoFocus
                text: qsTr("Reset")
                enabled: root.session.active
                onClicked: root.session.reset()
            }
            FbButton {
                objectName: "quitButton"
                Layout.minimumWidth: implicitWidth  // never squeezed; the title elides first
                visible: root.session.active
                implicitHeight: 36
                kind: "link"
                focusPolicy: Qt.NoFocus
                text: qsTr("Quit")
                onClicked: root.player.quitGame()
            }
            FbSegment {
                objectName: "tabSegment"
                Layout.minimumWidth: implicitWidth  // never squeezed; the title elides first
                current: root.tab
                options: root.session.active ? [
                    { value: "session", label: qsTr("Session"), name: "tabSession" },
                    { value: "multiview", label: qsTr("Multiview"), name: "tabMultiview" },
                    { value: "diagnostics", label: qsTr("Diagnostics"), name: "tabDiagnostics", underline: root.diag.open }
                ] : [
                    { value: "multiview", label: qsTr("Multiview"), name: "tabMultiview" },
                    { value: "diagnostics", label: qsTr("Diagnostics"), name: "tabDiagnostics", underline: root.diag.open }
                ]
                onPicked: (v) => root.ctl.tab = v
            }
            KeyHintButton {
                objectName: "addSessionToggle"
                Layout.minimumWidth: implicitWidth  // never squeezed; the title elides first
                visible: root.tab !== "session" && root.ctl.surfaceCount >= 1
                focusPolicy: Qt.NoFocus
                kind: root.pickerOpen ? "raised" : "outline"
                text: qsTr("+ Add to multiview")
                onClicked: { root.pickerToggled = true; root.pickerWanted = !root.pickerOpen }
            }
            LayoutSwitch {
                objectName: "layoutSwitch"
                Layout.minimumWidth: implicitWidth  // never squeezed; the title elides first
                layouts: root.screenLayouts
                compact: root.tab !== "session" && root.width < 1700
                current: root.ctl.screenLayout
                onPicked: (v) => root.ctl.screenLayout = v
            }
            KeyHintButton {
                objectName: "fullscreenButton"
                Layout.minimumWidth: implicitWidth  // never squeezed; the title elides first
                focusPolicy: Qt.NoFocus
                text: qsTr("Fullscreen")
                hint: "F11"
                onClicked: root.toggleFullscreen()
            }
        }
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

    // Session tab: game + side panel, overlay top left of the play area
    Item {
        objectName: "sessionTab"
        anchors.top: noticeBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: root.tab === "session"

        RowLayout {
            anchors.fill: parent
            spacing: 0

            GameView {
                id: view
                objectName: "gameView"
                Layout.fillWidth: true
                Layout.fillHeight: true
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
            SessionPanel {
                visible: !root.fullscreen
                Layout.fillHeight: true
                Layout.preferredWidth: visible ? 340 : 0
                player: root.player
            }
        }

        DiagnosticsOverlay {
            objectName: "diagnosticsOverlaySession"
            x: 16
            y: 16
            z: 10
            visible: root.diag.open
            model: root.diag
            fullscreen: root.fullscreen
        }
    }

    // Multiview
    ColumnLayout {
        objectName: "multiviewTab"
        anchors.top: noticeBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
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
                pickerOpen: root.pickerOpen
                onLocalReady: (v) => { root.multiLocal = v; Qt.callLater(root.focusLocal) }
                onPickerRequested: { root.pickerToggled = true; root.pickerWanted = true }
                onPickerCloseRequested: { root.pickerToggled = true; root.pickerWanted = false }
                onEscapePressed: root.handleEscape()
            }
            // Fullscreen: the same overlay, Emulation only (3y)
            DiagnosticsOverlay {
                objectName: "diagnosticsOverlayMulti"
                x: 16
                y: 16
                z: 10
                visible: root.diag.open && root.fullscreen
                model: root.diag
                fullscreen: true
            }
        }
        DiagnosticsBar {
            Layout.fillWidth: true
            visible: root.diag.open && !root.fullscreen
            model: root.diag
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
                implicitWidth: chipKey.implicitWidth + 10
                implicitHeight: 16
                radius: 3
                color: "transparent"
                border.width: 1
                border.color: Theme.borderButton
                FbMono { id: chipKey; anchors.centerIn: parent; text: "F3"; font.pixelSize: 10; color: Theme.textMuted }
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
                    hint: "F11 · Esc"
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
        text: qsTr("Toolbar appears when you move the mouse to the top · F3 diagnostics")
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
