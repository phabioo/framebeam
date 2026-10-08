import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Shared shell: Player mark, navigation, Hub switcher card and user block (docs/design/player.md "Common shell").
Rectangle {
    id: root
    required property PlayerController player
    color: Theme.bgSidebar
    implicitWidth: Theme.sidebarWidth

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.borderSidebar
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space16
        anchors.rightMargin: Theme.space16
        anchors.topMargin: 24
        anchors.bottomMargin: 24
        spacing: Theme.space28

        RowLayout {
            spacing: Theme.space10
            Layout.fillWidth: true
            Layout.leftMargin: 4
            // The header must never widen the column (and with it every fillWidth block below, e.g. the Now running
            // strip) when the font is wide: the title elides instead.
            Layout.minimumWidth: 0
            Logo { size: 22 }
            FbLabel {
                Layout.fillWidth: true
                text: qsTr("FrameBeam Player")
                elide: Text.ElideRight
                font.pixelSize: Theme.fontCard
                font.weight: Font.DemiBold
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Repeater {
                model: [
                    { label: qsTr("Library"), name: "navLibrary", target: "library" },
                    { label: qsTr("Emulation"), name: "navEmulation", target: "emulation" },
                    { label: qsTr("Controllers"), name: "navControllers", target: "controllers" },
                    { label: qsTr("Settings"), name: "navSettings", target: "settings" }
                ]
                delegate: Rectangle {
                    id: navItem
                    required property var modelData
                    objectName: modelData.name
                    // The model is constant (no binding on the screen), so delegates are never recreated.
                    readonly property bool active: modelData.target === root.player.screen
                    Layout.fillWidth: true
                    implicitHeight: 38
                    radius: Theme.radius6
                    color: navItem.active ? Theme.surfaceRaised : (navHover.hovered ? Theme.surface : "transparent")
                    Behavior on color { ColorAnimation { duration: Theme.durFast } }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        FbLabel {
                            Layout.fillWidth: true
                            text: navItem.modelData.label
                            font.weight: navItem.active ? Font.Medium : Font.Normal
                            color: navItem.active ? Theme.text : (navHover.hovered ? Theme.textSecondary : Theme.textMuted)
                            Behavior on color { ColorAnimation { duration: Theme.durFast } }
                        }
                        // A newer Player is available (3p): small accent dot on Settings.
                        Rectangle {
                            objectName: "settingsUpdateDot"
                            visible: navItem.modelData.target === "settings" && root.player.updates.updateAvailable
                            Layout.preferredWidth: 7
                            Layout.preferredHeight: 7
                            radius: 3.5
                            color: Theme.accent
                        }
                    }
                    HoverHandler { id: navHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            switch (navItem.modelData.target) {
                            case "settings": root.player.showSettings(); break
                            case "emulation": root.player.showEmulation(); break
                            case "controllers": root.player.showControllers(); break
                            default: root.player.showLibrary()
                            }
                        }
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }

        // Now running (3c-5): the game paused in the background, on every page; disappears when the game quits.
        Rectangle {
            id: nowRunning
            objectName: "nowRunningStrip"
            readonly property var game: root.player.backgroundGame
            readonly property bool shown: game.id !== undefined
            readonly property bool quitting: root.player.quitting
            property real nowMs: Date.now()
            readonly property int minutes: shown && game.startedMs ? Math.max(0, Math.floor((nowMs - game.startedMs) / 60000)) : 0
            visible: shown
            Layout.fillWidth: true
            implicitHeight: nrCol.implicitHeight + 24
            radius: Theme.radius9
            color: Theme.accentChipBg
            border.width: 1
            border.color: Theme.accentBoxBorder
            Timer { running: nowRunning.shown; interval: 30000; repeat: true; onTriggered: nowRunning.nowMs = Date.now() }
            onShownChanged: nowMs = Date.now()
            ColumnLayout {
                id: nrCol
                anchors.fill: parent
                anchors.margins: 12
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    FbMono {
                        Layout.minimumWidth: 0
                        text: qsTr("NOW RUNNING")
                        font.pixelSize: 10
                        font.letterSpacing: 0.8
                        font.weight: Font.Medium
                        color: Theme.accent
                    }
                    Item { Layout.fillWidth: true }
                    FbLabel {
                        objectName: "nowRunningTime"
                        Layout.minimumWidth: 0
                        elide: Text.ElideRight
                        text: qsTr("%1 min").arg(nowRunning.minutes)
                        font.pixelSize: 11
                        color: Theme.textMeta
                    }
                }
                FbLabel {
                    objectName: "nowRunningTitle"
                    Layout.fillWidth: true
                    text: nowRunning.game.title || ""
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.DemiBold
                }
                FbLabel {
                    objectName: "nowRunningStatus"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.fontMeta
                    color: Theme.infoText
                    text: nowRunning.quitting ? qsTr("Saving and syncing…")
                          : root.player.sessions.watching ? qsTr("Paused while you watch %1 · %2").arg(root.player.sessions.watchedWho).arg(root.player.sessions.watchedGame)
                          : qsTr("Paused")
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: 6
                    visible: !nowRunning.quitting
                    FbButton {
                        objectName: "nowRunningResume"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        kind: "primary"
                        implicitHeight: 30
                        implicitWidth: 0
                        font.pixelSize: Theme.fontSmall
                        text: qsTr("▶ Resume")
                        onClicked: root.player.resumeGame()
                    }
                    FbButton {
                        objectName: "nowRunningQuit"
                        implicitHeight: 30
                        implicitWidth: 0
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: quitProbe.implicitWidth + 20
                        font.pixelSize: Theme.fontSmall
                        text: qsTr("Quit")
                        background: Rectangle {
                            radius: Theme.radius6
                            color: parent.hovered ? Theme.surfaceRaised : "transparent"
                            border.width: 1
                            border.color: Theme.accentOutline
                        }
                        onClicked: root.player.requestQuit()
                        Text { id: quitProbe; visible: false; text: qsTr("Quit"); font.pixelSize: Theme.fontSmall }
                    }
                }
            }
        }

        // Hub switcher card
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: hubCol.implicitHeight + 24
            radius: Theme.radius10
            color: Theme.surfaceHub
            border.width: 1
            border.color: Theme.borderCard
            ColumnLayout {
                id: hubCol
                anchors.fill: parent
                anchors.margins: 12
                spacing: 6
                RowLayout {
                    spacing: Theme.space8
                    StatusDot { tone: "ok" }
                    FbLabel {
                        Layout.fillWidth: true
                        text: root.player.hubName
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }
                    FbButton {
                        objectName: "switchHubButton"
                        kind: "link"
                        text: qsTr("switch")
                        font.pixelSize: Theme.fontMeta
                        onClicked: root.player.switchHub()
                    }
                }
                FbMono {
                    Layout.fillWidth: true
                    text: qsTr("%1 · active hub").arg(root.player.hubAddress)
                    font.pixelSize: Theme.fontMono
                    elide: Text.ElideMiddle
                }
            }
        }

        // User block
        RowLayout {
            objectName: "userBlock"
            Layout.fillWidth: true
            Layout.leftMargin: 4
            spacing: Theme.space10
            Rectangle {
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                radius: 14
                color: Theme.surfaceRaised
                border.width: 1
                border.color: Theme.borderInput
                Text {
                    objectName: "userAvatarInitial"
                    anchors.centerIn: parent
                    text: (root.player.userName || root.player.deviceName || "?").charAt(0).toUpperCase()
                    font.pixelSize: Theme.fontMeta
                    font.weight: Font.DemiBold
                    color: Theme.textSecondary
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                // With a Hub user name: name on top, device name below. Older Hubs: device name + "This device".
                FbLabel {
                    id: userNameLabel
                    objectName: "userName"
                    visible: root.player.userName !== ""
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? implicitHeight : 0
                    text: root.player.userName
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
                FbLabel {
                    objectName: "userDevice"
                    Layout.fillWidth: true
                    text: root.player.deviceName
                    font.pixelSize: userNameLabel.visible ? Theme.fontMeta : Theme.fontSmall
                    font.weight: userNameLabel.visible ? Font.Normal : Font.Medium
                    color: userNameLabel.visible ? Theme.textMeta : Theme.text
                    elide: Text.ElideRight
                }
                FbLabel {
                    visible: !userNameLabel.visible
                    Layout.fillWidth: true
                    text: qsTr("This device")
                    font.pixelSize: Theme.fontMeta
                    color: Theme.textMeta
                    elide: Text.ElideRight
                }
            }
        }
    }
}
