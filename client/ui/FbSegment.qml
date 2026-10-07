import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Segment control (tokens.md): padding 3, radius 7, surface; active option #2c2d32 radius 5.
// options: [{ value, label, name, disabled, underline }] (name = objectName of the segment).
Rectangle {
    id: root
    property var options: []
    property string current: ""
    property bool stretch: false
    property int segmentHeight: 30
    signal picked(string value)

    // Never demands more than the room it is given (long labels elide) so a layout around it cannot grow past its parent.
    Layout.minimumWidth: 0
    implicitHeight: segmentHeight + 6
    implicitWidth: row.implicitWidth + 6
    radius: Theme.radius7
    color: Theme.surface
    border.width: 1
    border.color: Theme.borderCard

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
                Layout.minimumWidth: 0
                Layout.preferredWidth: root.stretch ? 1 : implicitWidth
                clip: true
                implicitWidth: segLabel.implicitWidth + 24
                radius: Theme.radius5
                color: active ? Theme.borderInput : (segHover.hovered && seg.modelData.disabled !== true ? Theme.surfaceRaised : "transparent")
                Behavior on color { ColorAnimation { duration: Theme.durFast } }
                Text {
                    id: segLabel
                    anchors.centerIn: parent
                    width: Math.min(implicitWidth, parent.width - 12)
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    text: seg.modelData.label
                    font.pixelSize: Theme.fontSmall
                    font.weight: seg.active ? Font.DemiBold : Font.Medium
                    color: seg.modelData.disabled === true ? Theme.textDisabled : (seg.active ? Theme.text : Theme.textMuted)
                }
                Rectangle {
                    visible: seg.modelData.underline === true
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width - 16
                    height: 2
                    color: Theme.accent
                }
                HoverHandler { id: segHover; cursorShape: seg.modelData.disabled === true ? Qt.ArrowCursor : Qt.PointingHandCursor }
                TapHandler { enabled: seg.modelData.disabled !== true; onTapped: root.picked(seg.modelData.value) }
            }
        }
    }
}
