import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Segment control. options: [{ value, label, name }] (name = objectName of the segment); dark: game header style.
Rectangle {
    id: root
    property var options: []
    property string current: ""
    property bool stretch: false
    property int segmentHeight: 30
    signal picked(string value)

    implicitHeight: segmentHeight + 6
    implicitWidth: row.implicitWidth + 6
    radius: 8
    color: Theme.surface
    border.width: 1
    border.color: Theme.borderInput

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: 3
        spacing: 2
        Repeater {
            model: root.options
            delegate: Rectangle {
                id: seg
                required property var modelData
                objectName: modelData.name || ""
                readonly property bool active: root.current === modelData.value
                Layout.fillWidth: root.stretch
                Layout.fillHeight: true
                implicitWidth: segLabel.implicitWidth + 24
                radius: 6
                color: active ? Theme.surfaceRaised : "transparent"
                border.width: active ? 1 : 0
                border.color: Theme.borderButton
                Text {
                    id: segLabel
                    anchors.centerIn: parent
                    text: seg.modelData.label
                    font.pixelSize: 13
                    font.weight: seg.active ? Font.DemiBold : Font.Medium
                    color: seg.active ? Theme.text : Theme.textMuted
                }
                Rectangle {
                    visible: seg.modelData.underline === true
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width - 16
                    height: 2
                    color: Theme.accent
                }
                TapHandler { onTapped: root.picked(seg.modelData.value) }
            }
        }
    }
}
