import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Player settings (Sidebar "Settings"): Appearance. Hubs are managed on the connection screen ("switch").
Rectangle {
    id: root
    required property PlayerController player
    color: Theme.bg

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            Layout.fillHeight: true
            Layout.preferredWidth: 232
            player: root.player
        }

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: content.implicitHeight + 64
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: content
                x: 36
                y: 28
                width: Math.min(640, flick.width - 72)
                spacing: 22

                ColumnLayout {
                    spacing: 2
                    FbLabel { text: qsTr("Settings"); font.pixelSize: 26; font.weight: Font.DemiBold; font.letterSpacing: -0.4 }
                    FbLabel {
                        text: qsTr("Settings apply locally to this device")
                        color: Theme.textMuted
                        font.pixelSize: 13
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: appearanceCol.implicitHeight + 40
                    radius: 10
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.borderCard

                    ColumnLayout {
                        id: appearanceCol
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 12
                        Eyebrow { text: qsTr("Appearance") }
                        FbSegment {
                            objectName: "appearanceSegment"
                            options: [
                                { value: "dark", label: qsTr("Dark"), name: "appearanceDark" },
                                { value: "light", label: qsTr("Light"), name: "appearanceLight" },
                                { value: "system", label: qsTr("System"), name: "appearanceSystem" }
                            ]
                            current: root.player.appearance
                            onPicked: value => root.player.appearance = value
                        }
                        FbLabel {
                            Layout.fillWidth: true
                            color: Theme.textMuted
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            text: qsTr("System follows the color scheme of the operating system where it is available. The game view always stays dark.")
                        }
                    }
                }
            }
        }
    }
}
