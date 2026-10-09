import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Segment of the in-game view (always dark): track #1a1b1e, padding 3, active #2c2d32. options: [{ value, label, name }].
// bordered/stretch = the panel variant (visibility).
Rectangle {
    id: root
    property var options: []
    property string current: ""
    property bool bordered: false
    property bool stretch: false
    property int padX: 11
    signal picked(string value)

    Layout.minimumWidth: 0
    Layout.alignment: Qt.AlignVCenter
    implicitHeight: 32
    implicitWidth: row.implicitWidth + 6
    radius: 7
    color: Theme.gameTrack
    border.width: bordered ? 1 : 0
    border.color: Theme.gameDivider

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
                implicitWidth: segLabel.implicitWidth + 2 * root.padX
                radius: 5
                color: active ? Theme.gameSegmentOn : (segHover.hovered ? Theme.gameHover : "transparent")
                Accessible.role: Accessible.Button
                Accessible.name: modelData.label
                Text {
                    id: segLabel
                    anchors.centerIn: parent
                    width: Math.min(implicitWidth, parent.width - 6)
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    text: seg.modelData.label
                    font.pixelSize: 13
                    font.weight: seg.active ? Font.Medium : Font.Normal
                    color: seg.active ? Theme.gameText : Theme.gameTextMuted
                }
                HoverHandler { id: segHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.picked(seg.modelData.value) }
            }
        }
    }
}
