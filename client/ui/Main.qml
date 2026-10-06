import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

ApplicationWindow {
    id: window
    required property PlayerController player

    visible: true
    width: 1280
    height: 800
    minimumWidth: 960
    minimumHeight: 600
    title: qsTr("FrameBeam Player")
    color: Theme.bg

    // Appearance (Settings): Dark | Light | System; the game view is always dark.
    Binding {
        target: Theme
        property: "dark"
        value: window.player.darkMode || window.player.screen === "game"
    }

    StackLayout {
        id: stack
        anchors.fill: parent
        currentIndex: ["connection", "pairing", "library", "game", "settings", "emulation", "controllers"].indexOf(window.player.screen)

        ConnectionScreen { player: window.player }
        PairingScreen { player: window.player }
        LibraryScreen { player: window.player }
        GameScreen { player: window.player }
        SettingsScreen { player: window.player }
        EmulationScreen { player: window.player }
        ControllersScreen { player: window.player }
    }

    // Emulation > FrameBeam > "Fullscreen on start": the window goes fullscreen while a game is shown and returns afterwards.
    property int visibilityBeforeGame: Window.Windowed
    property bool fullscreenApplied: false
    Connections {
        target: window.player
        function onScreenChanged() {
            if (window.player.screen === "game" && window.player.fullscreenOnStart && !window.fullscreenApplied) {
                window.visibilityBeforeGame = window.visibility
                window.fullscreenApplied = true
                window.visibility = Window.FullScreen
            } else if (window.player.screen !== "game" && window.fullscreenApplied) {
                window.fullscreenApplied = false
                window.visibility = window.visibilityBeforeGame === Window.FullScreen ? Window.Windowed : window.visibilityBeforeGame
            }
        }
    }

    ConflictDialog { player: window.player }
}
