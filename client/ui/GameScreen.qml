import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FrameBeam.Player

// Spielansicht: Header (56 px) und skalierter Frame auf schwarzem Hintergrund.
Rectangle {
    id: root
    required property PlayerController player
    readonly property GameSession session: player.gameSession
    color: Theme.gameBg

    onVisibleChanged: if (visible) view.forceActiveFocus()

    Rectangle {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56
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

            FbLabel {
                objectName: "gameTitle"
                Layout.fillWidth: true
                text: root.session.title
                color: Theme.gameText
                font.pixelSize: 16
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            FbMono {
                visible: root.width > 1100
                text: qsTr("Pfeile · X=A Z=B S=X A=Y · Q=L W=R · Enter=Start · Rücktaste=Select · Esc=Pause")
                font.pixelSize: 11
                color: Theme.gameTextMuted
            }
            FbButton {
                objectName: "pauseButton"
                implicitHeight: 36
                focusPolicy: Qt.NoFocus
                text: root.session.paused ? qsTr("Fortsetzen") : qsTr("Pause")
                enabled: root.session.state === GameSession.Running || root.session.state === GameSession.Paused
                onClicked: root.session.togglePause()
            }
            FbButton {
                objectName: "resetButton"
                implicitHeight: 36
                focusPolicy: Qt.NoFocus
                text: qsTr("Reset")
                enabled: root.session.active
                onClicked: root.session.reset()
            }
            FbButton {
                objectName: "quitButton"
                implicitHeight: 36
                focusPolicy: Qt.NoFocus
                text: qsTr("Beenden")
                onClicked: root.player.quitGame()
            }
        }
    }

    GameView {
        id: view
        objectName: "gameView"
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        session: root.session
        focus: true
        onEscapePressed: root.session.togglePause()

        FbLabel {
            anchors.centerIn: parent
            visible: !root.session.hasFrame && root.session.state !== GameSession.Failed
            text: qsTr("Emulator startet…")
            color: Theme.gameTextMuted
        }
        Rectangle {
            visible: root.session.paused
            anchors.fill: parent
            color: "#99000000"
            FbLabel {
                anchors.centerIn: parent
                text: qsTr("Pausiert · Esc oder Fortsetzen")
                color: Theme.gameText
                font.pixelSize: 18
                font.weight: Font.Medium
            }
        }
    }
}
