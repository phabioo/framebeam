import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// Game view (3g): header (56 px) with "← Library", title, Session pill and tabs Session | Multiview | Diagnostics;
// Session tab = local game + side panel (340), Multiview/Diagnostics = MultiviewArea (3h/3i). Without a local game
// (watching only) the remote Session fills the surface.
Rectangle {
    id: root
    required property PlayerController player
    readonly property GameSession session: player.gameSession
    readonly property SessionController ctl: player.sessions
    readonly property string tab: ctl.tab
    property Item multiLocal: null
    color: Theme.gameBg

    function focusLocal() {
        var v = root.tab === "session" ? view : root.multiLocal
        if (v) v.forceActiveFocus()
    }
    onVisibleChanged: if (visible) Qt.callLater(focusLocal)
    onTabChanged: Qt.callLater(focusLocal)

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

            FbButton {
                objectName: "backToLibraryButton"
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
                Layout.maximumWidth: 360
                text: root.session.active ? root.session.title : qsTr("Session from %1").arg(root.ctl.watchedWho)
                color: Theme.gameText
                font.pixelSize: 15
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Rectangle {
                objectName: "sharedPill"
                visible: root.ctl.shared
                implicitHeight: 26
                implicitWidth: pillText.implicitWidth + 24
                radius: 13
                color: Theme.okBg
                FbLabel {
                    id: pillText
                    anchors.centerIn: parent
                    text: qsTr("Session shared · %1 watching").arg(root.ctl.viewerCount)
                    color: Theme.ok
                    font.pixelSize: 12
                    font.weight: Font.Medium
                }
            }
            FbSegment {
                objectName: "modeSegment"
                visible: root.tab !== "session" && root.session.active && root.ctl.watching
                current: root.ctl.multiviewMode
                options: [
                    { value: "pip", label: qsTr("PiP"), name: "modePip" },
                    { value: "side", label: qsTr("Side-by-Side"), name: "modeSide" }
                ]
                onPicked: (v) => root.ctl.multiviewMode = v
            }
            Item { Layout.fillWidth: true }
            FbMono {
                visible: root.width > 1500 && root.tab === "session"
                text: qsTr("Arrows · X=A Z=B S=X A=Y · Q=L W=R · Enter=Start · Backspace=Select · Esc=Pause")
                font.pixelSize: 11
                color: Theme.gameTextMuted
            }
            FbButton {
                objectName: "pauseButton"
                visible: root.session.active
                implicitHeight: 36
                focusPolicy: Qt.NoFocus
                text: root.session.paused ? qsTr("Resume") : qsTr("Pause")
                enabled: root.session.state === GameSession.Running || root.session.state === GameSession.Paused
                onClicked: root.session.togglePause()
            }
            FbButton {
                objectName: "resetButton"
                visible: root.session.active
                implicitHeight: 36
                focusPolicy: Qt.NoFocus
                text: qsTr("Reset")
                enabled: root.session.active
                onClicked: root.session.reset()
            }
            FbButton {
                objectName: "quitButton"
                visible: root.session.active
                implicitHeight: 36
                focusPolicy: Qt.NoFocus
                text: qsTr("Quit")
                onClicked: root.player.quitGame()
            }
            FbSegment {
                objectName: "tabSegment"
                current: root.tab
                options: root.session.active ? [
                    { value: "session", label: qsTr("Session"), name: "tabSession" },
                    { value: "multiview", label: qsTr("Multiview"), name: "tabMultiview" },
                    { value: "diagnostics", label: qsTr("Diagnostics"), name: "tabDiagnostics", underline: root.tab === "diagnostics" }
                ] : [
                    { value: "multiview", label: qsTr("Multiview"), name: "tabMultiview" },
                    { value: "diagnostics", label: qsTr("Diagnostics"), name: "tabDiagnostics", underline: root.tab === "diagnostics" }
                ]
                onPicked: (v) => root.ctl.tab = v
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
        color: Theme.warnBg
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
                color: Theme.warn
            }
            FbButton { kind: "link"; focusPolicy: Qt.NoFocus; text: qsTr("Dismiss"); onClicked: root.player.saveHistory.dismissNotice() }
        }
    }

    // Session tab: game + side panel
    RowLayout {
        objectName: "sessionTab"
        anchors.top: noticeBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: root.tab === "session"
        spacing: 0

        GameView {
            id: view
            objectName: "gameView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            session: root.session
            focus: true
            onEscapePressed: root.session.togglePause()

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
            Layout.fillHeight: true
            Layout.preferredWidth: 340
            player: root.player
        }
    }

    // Multiview / Diagnostics
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
        MultiviewArea {
            Layout.fillWidth: true
            Layout.fillHeight: true
            player: root.player
            onLocalReady: (v) => { root.multiLocal = v; Qt.callLater(root.focusLocal) }
        }
        Rectangle {
            Layout.fillWidth: true
            visible: root.tab === "diagnostics"
            implicitHeight: diag.implicitHeight + 28
            color: Theme.gameHeader
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.gameBorder }
            DiagnosticsPanel {
                id: diag
                anchors.fill: parent
                anchors.margins: 14
                player: root.player
            }
        }
    }
}
