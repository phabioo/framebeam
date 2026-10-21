import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Segment of the in-game view (always dark): track #1a1b1e, padding 3, active #2c2d32. options: [{ value, label, name, disabled? }]; a disabled option is greyed and does not react.
// bordered/stretch = the panel variant (visibility).
Rectangle {
    id: root
    property var options: []
    property string current: ""
    property bool bordered: false
    property bool stretch: false
    property int padX: 11
    signal picked(string value)

    // Keyboard (R-P4): Tab reaches the segment, Left / Right move to the previous / next enabled option and pick it,
    // Enter / Space pick the focused one; the focused option gets an accent ring.
    property int focusIndex: -1
    function enabledAt(i) { return i >= 0 && i < options.length && options[i].disabled !== true }
    function currentIndex() {
        for (let i = 0; i < options.length; ++i) if (options[i].value === current && enabledAt(i)) return i
        for (let j = 0; j < options.length; ++j) if (enabledAt(j)) return j
        return -1
    }
    function step(dir) {
        let i = focusIndex < 0 ? currentIndex() : focusIndex
        for (let n = 0; n < options.length; ++n) {
            i += dir
            if (i < 0 || i >= options.length) return
            if (enabledAt(i)) { focusIndex = i; root.picked(options[i].value); return }
        }
    }
    activeFocusOnTab: true
    onActiveFocusChanged: if (activeFocus) focusIndex = currentIndex()
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Left) { step(-1); event.accepted = true }
        else if (event.key === Qt.Key_Right) { step(1); event.accepted = true }
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            if (enabledAt(focusIndex)) root.picked(options[focusIndex].value)
            event.accepted = true
        }
    }
    Accessible.role: Accessible.RadioButton

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
                required property int index
                objectName: modelData.name || ""
                readonly property bool off: modelData.disabled === true
                readonly property bool active: root.current === modelData.value && !off
                // fillWidth + maximumWidth = preferred: never grows, but may shrink (a non-fill item is never shrunk by the layout)
                Layout.fillWidth: true
                Layout.maximumWidth: root.stretch ? 100000 : implicitWidth
                Layout.fillHeight: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: implicitWidth   // stretch: the longest label gets the most room, so it truncates last ("Hub us…", R-P6)
                clip: true
                implicitWidth: segLabel.implicitWidth + 2 * root.padX
                radius: 5
                color: active ? Theme.gameSegmentOn : (segHover.hovered && !off ? Theme.gameHover : "transparent")
                Accessible.role: Accessible.Button
                Accessible.name: modelData.label
                Text {
                    id: segLabel
                    objectName: "segmentLabel"
                    anchors.centerIn: parent
                    width: Math.min(implicitWidth, parent.width - 6)
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    text: seg.modelData.label
                    textFormat: Text.PlainText
                    font.pixelSize: 13
                    font.weight: seg.active ? Font.Medium : Font.Normal
                    color: seg.active ? Theme.gameText : (seg.off ? Theme.gameFaint : Theme.gameTextMuted)
                }
                Rectangle {
                    objectName: "segmentFocusRing"
                    visible: root.activeFocus && root.focusIndex === seg.index
                    anchors.fill: parent
                    radius: seg.radius
                    color: "transparent"
                    border.width: 1.5
                    border.color: Theme.gameAccent
                }
                HoverHandler { id: segHover; cursorShape: seg.off ? Qt.ArrowCursor : Qt.PointingHandCursor }
                TapHandler { enabled: !seg.off; onTapped: root.picked(seg.modelData.value) }
            }
        }
    }
}
