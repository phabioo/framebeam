import QtQuick
import QtQuick.Window
import Uzume.App

// "Bottom screen in its own window" (NDS and 3DS, 0.19 package 6, board 3g-8): a second top-level window that shows only the
// bottom screen of your running game, scaled with the picture options of the game view. Mouse and touch there press the
// touch screen. It can be moved to another monitor; position, size and monitor are remembered on this PC (not per game).
// It is open while SessionController.bottomWindow is on and a game runs; closing the window turns the mode off, leaving
// the game closes it. The view always draws the CPU frame: the zero-copy texture of the game belongs to the main window.
Window {
    id: root
    property var player: null
    readonly property SessionController ctl: player ? player.sessions : null
    readonly property GameSession session: player ? player.gameSession : null
    // Keys the game view of this window did not use (hotkeys, Esc): the game screen runs them.
    signal hotkeyKey(var event)

    readonly property bool wanted: !!ctl && !!session && ctl.bottomWindow && session.active && session.screenCount >= 2
    property bool placed: false

    transientParent: null
    title: session ? qsTr("%1 · bottom screen").arg(session.title) : qsTr("Bottom screen")
    color: Theme.ground
    width: 640
    height: 480
    minimumWidth: 160
    minimumHeight: 120
    visible: false

    // Restores the saved place when that monitor still exists; else the window opens on the main monitor, centered.
    function place() {
        const g = player ? player.bottomWindowGeometry() : ({})
        let screen = null
        if (g.width > 0) {
            const screens = Qt.application.screens
            for (let i = 0; i < screens.length; ++i)
                if (screens[i].name === g.screen) screen = screens[i]
        }
        if (screen) {
            root.screen = screen
            root.width = g.width
            root.height = g.height
            root.x = g.x
            root.y = g.y
        } else {
            const sc = root.screen
            if (sc) {
                root.x = sc.virtualX + Math.round((sc.width - root.width) / 2)
                root.y = sc.virtualY + Math.round((sc.height - root.height) / 2)
            }
        }
        root.placed = true
    }
    function show_() {
        if (!root.placed) root.place()
        root.visible = true
    }
    onWantedChanged: {
        if (wanted) show_()
        else root.visible = false
    }
    Component.onCompleted: if (wanted) show_()
    // Closing the window (title bar, Alt+F4) turns the mode off, the main view shows both screens again.
    onClosing: if (root.ctl) root.ctl.bottomWindow = false

    // Remember position, size and monitor (debounced; not while minimized or fullscreen).
    Timer {
        id: saveTimer
        interval: 600
        onTriggered: if (root.player && root.visible && root.visibility === Window.Windowed && root.screen)
            root.player.saveBottomWindowGeometry(root.x, root.y, root.width, root.height, root.screen.name)
    }
    onXChanged: if (visible) saveTimer.restart()
    onYChanged: if (visible) saveTimer.restart()
    onWidthChanged: if (visible) saveTimer.restart()
    onHeightChanged: if (visible) saveTimer.restart()

    Item {
        anchors.fill: parent
        focus: true
        Keys.onPressed: (e) => root.hotkeyKey(e)
        GameView {
            id: view
            objectName: "bottomScreenView"
            anchors.fill: parent
            anchors.margins: Theme.space1
            session: root.session
            layout: "bottom"
            cpuOnly: true
            strictInteger: root.ctl ? root.ctl.integerScale : false
            aspectRatio: root.ctl ? root.ctl.aspectRatio : "core"
            focus: true
            onEscapePressed: root.hotkeyKey({ key: Qt.Key_Escape, modifiers: 0, isAutoRepeat: false, accepted: false })
        }
    }
}
