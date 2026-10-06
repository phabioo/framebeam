import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Shared shell: navigation and hub switcher.
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
                    { label: qsTr("Library"), enabled: true, name: "navLibrary", target: "library" },
                    { label: qsTr("Emulation"), enabled: false, name: "navEmulation", target: "" },
                    { label: qsTr("Controllers"), enabled: false, name: "navControllers", target: "" },
                    { label: qsTr("Settings"), enabled: true, name: "navSettings", target: "settings" }
                ]
                delegate: Rectangle {
                    id: navItem
                    required property var modelData
                    objectName: modelData.name
                    // The model is constant (no binding on the screen), so delegates are never recreated.
                    readonly property bool active: modelData.target !== "" && modelData.target === root.player.screen
                    Layout.fillWidth: true
                    implicitHeight: 38
                    radius: 6
                    color: navItem.active ? Theme.surfaceRaised : "transparent"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        FbLabel {
                            Layout.fillWidth: true
                            text: navItem.modelData.label
                            font.weight: navItem.active ? Font.Medium : Font.Normal
                            color: navItem.active ? Theme.text : (navItem.modelData.enabled ? Theme.textMuted : Theme.textDisabled)
                        }
                        FbMono {
                            visible: !navItem.modelData.enabled
                            text: qsTr("soon")
                            font.pixelSize: 11
                            color: Theme.textDisabled
                        }
                    }
                    TapHandler {
                        enabled: navItem.modelData.enabled
                        onTapped: navItem.modelData.target === "settings" ? root.player.showSettings() : root.player.showLibrary()
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }

        // Hub switcher card
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
                        text: qsTr("switch")
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
                    text: qsTr("active hub")
                    font.pixelSize: 11
                }
            }
        }
    }
}
