import QtQuick
import QtQuick.Controls
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

    StackLayout {
        id: stack
        anchors.fill: parent
        currentIndex: ["connection", "pairing", "library", "game"].indexOf(window.player.screen)

        ConnectionScreen { player: window.player }
        PairingScreen { player: window.player }
        LibraryScreen { player: window.player }
        GameScreen { player: window.player }
    }
}
