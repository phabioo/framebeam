import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// 3h / 3i: Multiview with the local Session and one remote Session. Side-by-side or PiP; without a local game
// the remote Session fills the surface, without a remote Session the local game does. Exactly one surface is audible.
Item {
    id: root
    required property PlayerController player
    readonly property SessionController ctl: player.sessions
    readonly property bool hasLocal: player.gameSession.active
    readonly property bool hasRemote: ctl.watching
    readonly property bool side: ctl.multiviewMode === "side"
    readonly property string localName: qsTr("You · %1").arg(ctl.localTitle)
    readonly property string remoteName: qsTr("%1 · %2").arg(ctl.watchedWho).arg(ctl.watchedGame)
    signal localReady(Item view)

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
        }
    }

    // Header of a surface: avatar, "who · game", meta, audio button.
    component SurfaceHeader: RowLayout {
        id: sh
        property string name
        property string meta
        property string surface   // "local" | "remote"
        readonly property bool audible: root.ctl.audioFocus === surface
        spacing: 10
        Rectangle {
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
            radius: 14
            color: Theme.surfaceRaised
            Text {
                anchors.centerIn: parent
                text: (sh.name.length > 0 ? sh.name.charAt(0) : "?").toUpperCase()
                font.pixelSize: 12
                font.weight: Font.DemiBold
                color: Theme.gameText
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            FbLabel { Layout.fillWidth: true; text: sh.name; elide: Text.ElideRight; font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.gameText }
            FbLabel { text: sh.meta; font.pixelSize: 12; color: Theme.gameTextMuted }
        }
        FbButton {
            objectName: sh.surface === "local" ? "audioLocalButton" : "audioRemoteButton"
            implicitHeight: 30
            focusPolicy: Qt.NoFocus
            font.pixelSize: 12
            kind: sh.audible ? "primary" : "outline"
            text: sh.audible ? qsTr("Audio on") : qsTr("Audio here")
            enabled: !sh.audible
            onClicked: root.ctl.audioHere(sh.surface)
        }
    }

    // Only the local game: full surface (and a hint).
    Item {
        anchors.fill: parent
        visible: root.hasLocal && !root.hasRemote
        Loader {
            id: localAlone
            anchors.fill: parent
            sourceComponent: (root.hasLocal && !root.hasRemote) ? localComp : undefined
        }
        // No remote Session yet: list the Sessions of this Hub, watching one adds it next to the local game.
        Rectangle {
            id: pick
            objectName: "multiviewSessionList"
            readonly property var list: root.ctl.sessions
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 14
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
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    elide: Text.ElideRight
                    text: qsTr("Watch a Session next to your game")
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
                        objectName: "multiviewSession_" + modelData.sessionId
                        width: ListView.view.width - (pickView.contentHeight > pickView.height ? 12 : 0)
                        spacing: 10
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: 0
                            spacing: 0
                            FbLabel { Layout.fillWidth: true; elide: Text.ElideRight; text: prow.modelData.title; font.pixelSize: 13; color: Theme.gameText }
                            FbLabel { Layout.fillWidth: true; elide: Text.ElideRight; text: prow.modelData.meta; font.pixelSize: 11; color: prow.modelData.invited ? Theme.accent : Theme.gameTextMuted }
                        }
                        FbButton {
                            objectName: "multiviewWatchButton_" + prow.index
                            implicitHeight: 30
                            kind: "primary"
                            focusPolicy: Qt.NoFocus
                            font.pixelSize: 12
                            text: prow.modelData.invited ? qsTr("Join") : qsTr("Watch Session")
                            enabled: root.ctl.hubLink === "online" && !root.ctl.joining
                            onClicked: root.ctl.watch(prow.modelData.sessionId)
                        }
                    }
                }
            }
        }
    }

    // Only a remote Session (no local game).
    ColumnLayout {
        anchors.fill: parent
        visible: !root.hasLocal && root.hasRemote
        spacing: 0
        SurfaceHeader {
            Layout.fillWidth: true
            Layout.margins: 12
            name: root.remoteName
            meta: qsTr("Session from %1").arg(root.ctl.watchedWho)
            surface: "remote"
        }
        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: (!root.hasLocal && root.hasRemote) ? remoteComp : undefined
        }
    }

    // Side by side (3h)
    Rectangle {
        objectName: "sideBySide"
        anchors.fill: parent
        visible: root.hasLocal && root.hasRemote && root.side
        color: Theme.gameBorder
        RowLayout {
            anchors.fill: parent
            spacing: 2
            Repeater {
                model: root.ctl.swapped ? ["remote", "local"] : ["local", "remote"]
                delegate: Rectangle {
                    id: col
                    required property string modelData
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    color: Theme.gameBg
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0
                        SurfaceHeader {
                            Layout.fillWidth: true
                            Layout.margins: 12
                            surface: col.modelData
                            name: col.modelData === "local" ? root.localName : root.remoteName
                            meta: col.modelData === "local" ? qsTr("local") : qsTr("Session from %1").arg(root.ctl.watchedWho)
                        }
                        Loader {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            sourceComponent: (root.hasLocal && root.hasRemote && root.side) ? (col.modelData === "local" ? localComp : remoteComp) : undefined
                        }
                    }
                }
            }
        }
    }

    // Picture in picture (3i)
    Item {
        id: pip
        objectName: "pipMode"
        anchors.fill: parent
        visible: root.hasLocal && root.hasRemote && !root.side
        readonly property string mainSurface: root.ctl.swapped ? "remote" : "local"
        readonly property string pipSurface: root.ctl.swapped ? "local" : "remote"
        Loader {
            anchors.fill: parent
            sourceComponent: (root.hasLocal && root.hasRemote && !root.side) ? (pip.mainSurface === "local" ? localComp : remoteComp) : undefined
        }
        Rectangle {
            id: pipWindow
            objectName: "pipWindow"
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 28
            width: 248
            height: pipCol.implicitHeight + 16
            radius: 10
            color: Theme.bgPanel
            border.width: 1
            border.color: Theme.gameBorder
            ColumnLayout {
                id: pipCol
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    StatusDot { tone: "ok" }
                    FbLabel {
                        Layout.fillWidth: true
                        text: pip.pipSurface === "remote" ? root.remoteName : root.localName
                        elide: Text.ElideRight
                        font.pixelSize: 12
                        color: Theme.gameText
                    }
                    FbLabel {
                        objectName: "pipAudioState"
                        visible: root.ctl.audioFocus === pip.pipSurface
                        text: qsTr("audio on")
                        font.pixelSize: 11
                        color: Theme.ok
                    }
                    FbButton {
                        objectName: "pipAudioButton"
                        visible: root.ctl.audioFocus !== pip.pipSurface
                        kind: "link"
                        focusPolicy: Qt.NoFocus
                        font.pixelSize: 11
                        text: qsTr("muted · Audio here")
                        onClicked: root.ctl.audioHere(pip.pipSurface)
                    }
                }
                Loader {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(300, Math.max(120, root.height * 0.45))
                    sourceComponent: (root.hasLocal && root.hasRemote && !root.side) ? (pip.pipSurface === "local" ? localComp : remoteComp) : undefined
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    FbButton {
                        objectName: "swapButton"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        implicitHeight: 32
                        focusPolicy: Qt.NoFocus
                        font.pixelSize: 13
                        text: qsTr("Swap")
                        onClicked: root.ctl.swapSurfaces()
                    }
                    FbButton {
                        objectName: "removeRemoteButton"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        implicitHeight: 32
                        focusPolicy: Qt.NoFocus
                        font.pixelSize: 13
                        text: qsTr("Remove")
                        onClicked: root.ctl.leaveWatch()
                    }
                }
            }
        }
        FbButton {
            objectName: "mainAudioButton"
            visible: root.ctl.audioFocus !== pip.mainSurface
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 12
            implicitHeight: 28
            focusPolicy: Qt.NoFocus
            font.pixelSize: 12
            text: qsTr("Audio here")
            onClicked: root.ctl.audioHere(pip.mainSurface)
        }
    }
}
