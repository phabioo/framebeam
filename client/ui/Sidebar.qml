import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Gemeinsame Shell: Navigation und Hub-Switcher.
Rectangle {
    id: root
    required property PlayerController player
    color: Theme.bgSidebar
    implicitWidth: 232

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.borderSidebar
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        anchors.topMargin: 24
        anchors.bottomMargin: 24
        spacing: 28

        RowLayout {
            spacing: 10
            Layout.leftMargin: 4
            Logo { }
            FbLabel { text: qsTr("FrameBeam Player"); font.pixelSize: 16; font.weight: Font.DemiBold }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Repeater {
                model: [
                    { label: qsTr("Library"), active: true },
                    { label: qsTr("Emulation"), active: false },
                    { label: qsTr("Controllers"), active: false },
                    { label: qsTr("Settings"), active: false }
                ]
                delegate: Rectangle {
                    id: navItem
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 38
                    radius: 6
                    color: modelData.active ? Theme.surfaceRaised : "transparent"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        FbLabel {
                            Layout.fillWidth: true
                            text: navItem.modelData.label
                            font.weight: navItem.modelData.active ? Font.Medium : Font.Normal
                            color: navItem.modelData.active ? Theme.text : Theme.textDisabled
                        }
                        FbMono {
                            visible: !navItem.modelData.active
                            text: qsTr("folgt")
                            font.pixelSize: 11
                            color: Theme.textDisabled
                        }
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }

        // Hub-Switcher-Karte
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: hubCol.implicitHeight + 24
            radius: 10
            color: Theme.surfaceHub
            border.width: 1
            border.color: Theme.borderCard
            ColumnLayout {
                id: hubCol
                anchors.fill: parent
                anchors.margins: 12
                spacing: 6
                RowLayout {
                    spacing: 8
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
                        text: qsTr("wechseln")
                        onClicked: root.player.switchHub()
                    }
                }
                FbMono {
                    Layout.fillWidth: true
                    text: root.player.hubAddress
                    font.pixelSize: 11
                    elide: Text.ElideMiddle
                }
                FbMono {
                    text: qsTr("aktiver Hub")
                    font.pixelSize: 11
                }
            }
        }
    }
}
