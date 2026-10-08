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
            Layout.leftMargin: 4
            Logo { size: 22 }
            FbLabel { text: qsTr("FrameBeam Player"); font.pixelSize: Theme.fontCard; font.weight: Font.DemiBold }
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
